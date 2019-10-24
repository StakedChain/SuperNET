/******************************************************************************
 * Copyright © 2014-2018 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/
 
#define DPOW_BLACKLIST -100000

void dpow_bestmask_update(struct supernet_info *myinfo,struct dpow_info *dp,struct dpow_block *bp,uint8_t nn_senderind,int8_t nn_bestk,uint64_t nn_bestmask,uint64_t nn_recvmask)
{
    int32_t startscore;
    if ( nn_senderind < 0 || nn_senderind >= bp->numnotaries )
        return;
    bp->notaries[nn_senderind].bestk = nn_bestk;
    bp->notaries[nn_senderind].bestmask = nn_bestmask;
    bp->notaries[nn_senderind].recvmask |= nn_recvmask;
    startscore = bp->scores[nn_senderind];
    if ( bp->bestk >= 0 )
    {
        if ( nn_bestk < 0 )
            bp->scores[nn_senderind] -= 10;
        else if ( nn_bestk != bp->bestk )
            bp->scores[nn_senderind]--;
        else if ( nn_bestmask != bp->bestmask )
            bp->scores[nn_senderind]--;
        else if ( bp->scores[nn_senderind] < 1 )
            bp->scores[nn_senderind] = 1;
        else bp->scores[nn_senderind]++;
        if ( startscore > DPOW_BLACKLIST && bp->scores[nn_senderind] <= DPOW_BLACKLIST )
            printf(">>>>>>>>>>>>> nn_senderind.%d %llx MIA, skip this node for now\n",nn_senderind,(long long)(1LL << nn_senderind));
    }
}

uint64_t dpow_lastk_mask(struct dpow_block *bp,int8_t *lastkp)
{
    int32_t j,m,k; uint64_t mask = bp->require0;
    *lastkp = -1;
    m = bp->require0;
    for (j=0; j<bp->numnotaries; j++)
    {
        k = DPOW_MODIND(bp,j,DPOW_CHECKPOINTFREQ);
        if ( (bp->require0 == 0 || k != 0) && bp->scores[k] < DPOW_BLACKLIST )
            continue;
        if ( bits256_nonz(bp->notaries[k].src.prev_hash) != 0 && bits256_nonz(bp->notaries[k].dest.prev_hash) != 0 )
        {
            bp->recvmask |= (1LL << k);
            mask |= (1LL << k);
            if ( ++m == bp->minsigs )
            {
                *lastkp = k;
                break;
            }
        }
    }
    return(mask);
}

int32_t dpow_bestk(struct dpow_block *bp,uint64_t *maskp)
{
    int8_t lastk; uint64_t mask;
    *maskp = 0;
    mask = dpow_lastk_mask(bp,&lastk);
    if ( lastk < 0 )
        return(-1);
    *maskp = mask;
    return(lastk);
}

uint64_t dpow_ratifybest(uint64_t refmask,struct dpow_block *bp,int8_t *lastkp)
{
    int32_t m,j,k; uint64_t bestmask,mask = bp->require0;
    bestmask = 0;
    *lastkp = -1;
    for (m=j=0; j<bp->numnotaries; j++)
    {
        k = (j + ((uint32_t)time(NULL) / DPOW_EPOCHDURATION)) % bp->numnotaries;//DPOW_MODIND(bp,j);
        if ( bp->require0 != 0 && k == 0 )
            continue;
        if ( bits256_nonz(bp->notaries[k].ratifysrcutxo) != 0 && bits256_nonz(bp->notaries[k].ratifydestutxo) != 0 )
        {
            mask |= (1LL << k);
            if ( ++m == bp->minsigs-bp->require0 )
            {
                *lastkp = k;
                bestmask = mask | bp->require0;
                //printf("m.%d == minsigs.%d (%d %llx)\n",m,bp->minsigs,k,(long long)bestmask);
            }
        }
    }
    return(bestmask);
}

uint64_t dpow_notarybestk(uint64_t refmask,struct dpow_block *bp,int8_t *lastkp)
{
    int32_t m,j,k,z,n; int8_t bestk = -1; uint64_t bestmask,mask = 0;//bp->require0;
    bestmask = 0;
    for (m=j=0; j<bp->numnotaries; j++)
    {
        //k = (j + ((uint32_t)time(NULL) / 180)) % bp->numnotaries;
        k = (j + (bp->height/DPOW_CHECKPOINTFREQ)) % bp->numnotaries;
        //if ( bp->require0 != 0 && k == 0 )
        //    continue;
        if ( bits256_nonz(bp->notaries[k].src.prev_hash) != 0 && bits256_nonz(bp->notaries[k].dest.prev_hash) != 0 && bp->paxwdcrc == bp->notaries[k].paxwdcrc )
        {
            for (z=n=0; z<bp->numnotaries; z++)
                if ( (bp->notaries[z].recvmask & (1LL << k)) != 0 )
                    n++;
            if ( n >= bp->numnotaries/2 )
            {
                mask |= (1LL << k);
                if ( ++m == bp->minsigs )//-bp->require0 )
                {
                    bestk = k;
                    bestmask = mask;// | bp->require0;
                    //printf("m.%d == minsigs.%d (%d %llx)\n",m,bp->minsigs,k,(long long)bestmask);
                }
            }
        }
    }
    if ( bestk >= 0 )
        *lastkp = bestk;
    return(bestmask);
}

uint64_t dpow_maskmin(uint64_t refmask, struct dpow_info *dp,struct dpow_block *bp,int8_t *lastkp)
{
    int32_t j,m,k,z,n,i,p,jk; uint64_t bestmask = 0,mask = 0; uint8_t rndnodes[32];
    *lastkp = -1;
    m = 0;
    /* 
        Need to make sure majority of nodes agree on recvmask before calcuating and submitting bestmask and besk. 
        This keeps the network in consensus. 
        Notarizations are more reliable and better distributed amoung all nodes who are in a working state, rather than 
        the first 13 nodes who see a bestk that is valid in their recvmask. 
        nodes can only get into recvmask if they have utxos submitted. This happens in dpow_notarize_update before this function is called.
    */
    for (z=n=0; z<bp->numnotaries; z++)
        if ( bitweight(bp->notaries[z].recvmask) >= bp->minnodes )
            n++;
    if ( n < bp->minnodes )
        return(bestmask);

    if ( bp->newconsensus != 0 )
    {
        // Fetch the last nota txid and create a seed from it. 
        printf(BLUE"Random seed: ");
        for ( i=0; i<32; i++ )
        {
            // 32 rnd numbers from 1 -> numnotaries-1. 
            rndnodes[i] = (dp->prevnotatxid.bytes[i] % (bp->numnotaries-1))+1;
            printf("%i, ", rndnodes[i]);
        }
        printf("\n"RESET);
    }
    z = -1;
    for (j=0; j<bp->numnotaries; j++)
    {
        if ( bp->newconsensus != 0 )
        {
            jk = ((k=i= DPOW_MODIND(bp,j,dp->freq))>>1);
            for ( p=0; p<32; p++ )
            {
                if ( (bp->recvmask & (1LL << k)) != 0 && (mask & (1LL << k)) == 0 )
                    break;
                jk = ((jk >= 32) ? 0 : jk+1);
                k += rndnodes[jk];
                while ( k >= bp->numnotaries ) 
                    k -= bp->numnotaries;
            }
            // no need to check utxo's as they are checked to enter recvmask. 
            if ( bp->paxwdcrc != bp->notaries[k].paxwdcrc || p == 32 ) 
                continue;
            mask |= (1LL << k);
            if ( ++m == bp->minsigs )
            {
                *lastkp = i;
                bestmask = mask;
                z = k;
            }
        }
        else 
        {
            k = DPOW_MODIND(bp,j,DPOW_CHECKPOINTFREQ);
            mask |= (1LL << k);
            if ( ++m == bp->minsigs )
            {
                *lastkp = k;
                bestmask = mask;
            }
        }
    }
    if ( *lastkp >= 0 )
    {
        char str[64]; sprintf(str,CYAN"-> newk.%i"RESET, z); 
        printf(GREEN"[%s:%i] nodes.%i vs min.%i bestk.%i %s\n", bp->srccoin->symbol, bp->height, bitweight(bp->recvmask), bp->minnodes, *lastkp, (*lastkp != z && bp->newconsensus != 0) ? str : RESET);
        for (mask=j=0; j<bp->numnotaries; j++)
        {
            if ( bp->notaries[j].src.siglens[*lastkp] > 0 )
                mask |= (1LL << j);
        }
        bp->srcsigsmasks[*lastkp] |= mask;
        for (mask=j=0; j<bp->numnotaries; j++)
        {
            if ( bp->notaries[j].dest.siglens[*lastkp] > 0 )
                mask |= (1LL << j);
        }
        bp->destsigsmasks[*lastkp] |= mask;
   }
   return(bestmask);
}

struct dpow_block *dpow_heightfind(struct supernet_info *myinfo,struct dpow_info *dp,int32_t height)
{
    int32_t i; struct dpow_block *bp = 0;
    for (i = 0; i < dp->maxblocks; i++) 
    {
        if ( dp->blocks[i] != 0 && height == dp->blocks[i]->height )
            return(dp->blocks[i]);
    }
    return(bp);
}

int32_t dpow_heightfind2(struct supernet_info *myinfo,struct dpow_info *dp,int32_t height)
{
    int32_t i; struct dpow_block *bp = 0;
    for (i = 0; i < dp->maxblocks; i++) 
    {
        if ( dp->blocks[i] != 0 && height == dp->blocks[i]->height )
            fprintf(stderr, "FOUND: blockindex.%i\n", i);
    }
    return(0);
}

int32_t dpow_blockfind(struct supernet_info *myinfo,struct dpow_info *dp)
{
    int32_t i;
    for (i = 0; i < dp->maxblocks; i++) 
    {
        if ( dp->blocks[i] == 0 )
            return(i);
    }
    return(-1);
}

int32_t dpow_voutstandard(struct supernet_info *myinfo,struct dpow_block *bp,uint8_t *serialized,int32_t m,int32_t src_or_dest,uint8_t pubkeys[][33],int32_t numratified)
{
    uint32_t paxwdcrc=0,locktime=0,numvouts; struct iguana_info *coin; uint64_t satoshis,satoshisB; int32_t i,n=0,opretlen,len=0; uint8_t opret[16384],data[16384],extras[16384];
    numvouts = 2;
    if ( pubkeys == 0 || numratified <= 0 )
    {
        if ( src_or_dest != 0 )
            coin = bp->destcoin;
        else coin = bp->srccoin;
        satoshis = DPOW_UTXOSIZE * m * .76;
        if ( (satoshisB= DPOW_UTXOSIZE * m - 10000) < satoshis )
            satoshis = satoshisB;
    }
    else
    {
        satoshis = DPOW_MINOUTPUT;
        numvouts += numratified;
    }
    len += iguana_rwvarint32(1,&serialized[len],&numvouts);
    len += iguana_rwnum(1,&serialized[len],sizeof(satoshis),&satoshis);
    serialized[len++] = 35;
    serialized[len++] = 33;
    decode_hex(&serialized[len],33,CRYPTO777_PUBSECPSTR), len += 33;
    serialized[len++] = CHECKSIG;
    if ( pubkeys != 0 && numratified != 0 )
    {
        satoshis = DPOW_MINOUTPUT;
        for (i=0; i<numratified; i++)
        {
            len += iguana_rwnum(1,&serialized[len],sizeof(satoshis),&satoshis);
            serialized[len++] = 35;
            serialized[len++] = 33;
            memcpy(&serialized[len],pubkeys[i],33), len += 33;
            serialized[len++] = CHECKSIG;
        }
        printf("numvouts.%d len.%d RATIFY vouts\n",numvouts,len);
    }
    if ( bp->MoMdepth > 0 && strcmp(bp->destcoin->symbol,"KMD") == 0 ) // || strcmp(bp->srccoin->symbol,"KMD") == 0) )
    {
        n = dpow_paxpending(myinfo,extras,sizeof(extras),&paxwdcrc,bp->MoM,bp->MoMdepth,bp->CCid,src_or_dest,bp);
    }
    satoshis = 0;
    len += iguana_rwnum(1,&serialized[len],sizeof(satoshis),&satoshis);
    if ( bp->isratify != 0 )
        opretlen = dpow_rwopret(1,opret,&bp->hashmsg,&bp->height,bp->srccoin->symbol,0,0,bp,src_or_dest);
    else opretlen = dpow_rwopret(1,opret,&bp->hashmsg,&bp->height,bp->srccoin->symbol,extras,n,bp,src_or_dest);
    if ( opretlen < 0 )
    {
        printf("negative opretlen.%d src_or_dest.%d\n",opretlen,src_or_dest);
        return(-1);
    }
    opretlen = dpow_opreturnscript(data,opret,opretlen);
    if ( opretlen < 0xfd )
        serialized[len++] = opretlen;
    else
    {
        serialized[len++] = 0xfd;
        serialized[len++] = opretlen & 0xff;
        serialized[len++] = (opretlen >> 8) & 0xff;
    }
    memcpy(&serialized[len],data,opretlen), len += opretlen;
    len += iguana_rwnum(1,&serialized[len],sizeof(locktime),&locktime);
    return(len);
}

bits256 dpow_notarytx(struct supernet_info *myinfo,char *signedtx,int32_t *numsigsp,int32_t isPoS,struct dpow_block *bp,int8_t bestk,uint64_t bestmask,int32_t usesigs,int32_t src_or_dest,uint8_t pubkeys[][33],int32_t numratified)
{
    uint32_t k,m,numsigs,version,vout,crcval,sequenceid = 0xffffffff; bits256 zero; int32_t n,siglen,len; uint8_t serialized[32768],*sig; bits256 txid; struct dpow_entry *ep; struct dpow_coinentry *cp;
    // int32_t preimage_len; uint8_t preimage[32768]; // here we will create preimage, when usesigs=0 (?)

	struct iguana_info *coin = (src_or_dest != 0) ? bp->destcoin : bp->srccoin;
	//printf("[Decker] dpow_notarytx: src.(%s) dst.(%s) src_or_dest.(%d) usesigs.(%d)\n", bp->srccoin->symbol, bp->destcoin->symbol, src_or_dest, usesigs);

	signedtx[0] = 0;
    *numsigsp = 0;
    memset(zero.bytes,0,sizeof(zero));
    len = numsigs = 0;
    version = 1;

	if (coin->sapling != 0) {
		version = 4;
		version = 1 << 31 | version; // overwintered
	}

	len += iguana_rwnum(1,&serialized[len],sizeof(version),&version);

	if (coin->sapling != 0) {
		uint32_t versiongroupid = 0x892f2085; // sapling
		len += iguana_rwnum(1, &serialized[len], sizeof(versiongroupid), &versiongroupid);
	}

	if ( isPoS != 0 )
        len += iguana_rwnum(1,&serialized[len],sizeof(bp->timestamp),&bp->timestamp);
    m = bp->minsigs;
    len += iguana_rwvarint32(1,&serialized[len],(uint32_t *)&m);
    // -- vins --
	for (k=m=0; k<bp->numnotaries; k++)
    {
        siglen = 0;
        sig = 0;
        if ( ((1LL << k) & bestmask) != 0 )
        {
            if ( pubkeys != 0 && numratified > 0 ) // state [1]
            {
		//printf("[Decker] dpow_notarytx: state [1]\n");
                if ( src_or_dest != 0 )
                {
                    txid = bp->notaries[k].ratifydestutxo;
                    vout = bp->notaries[k].ratifydestvout;
                }
                else
                {
                    txid = bp->notaries[k].ratifysrcutxo;
                    vout = bp->notaries[k].ratifysrcvout;
                }

				if ( bestk >= 0 ) //
                {
                    siglen = bp->notaries[k].ratifysiglens[src_or_dest];
                    sig = bp->notaries[k].ratifysigs[src_or_dest];
                }
                //char str[65]; printf("j.%d k.%d m.%d vin.(%s) v%d siglen.%d\n",j,k,m,bits256_str(str,txid),vout,siglen);
            }
            else // state [2]
            {
		//printf("[Decker] dpow_notarytx: state [2]\n");
                ep = &bp->notaries[k];
                cp = (src_or_dest != 0) ? &bp->notaries[k].dest : &bp->notaries[k].src;
                if ( bits256_nonz(cp->prev_hash) == 0 )
                {
                    printf("null prevhash k.%d m.%d src_or_dest.%d\n",k,m,src_or_dest);
                    return(zero);
                }
                txid = cp->prev_hash;
                vout = cp->prev_vout;
                if ( bestk >= 0 )
                {
                    siglen = cp->siglens[bestk];
                    sig = cp->sigs[bestk];
                }
            }
            len += iguana_rwbignum(1,&serialized[len],sizeof(txid),txid.bytes);
            len += iguana_rwnum(1,&serialized[len],sizeof(vout),&vout);

			if ( usesigs != 0 && bestk >= 0 ) // usesigs=1 -> insert signature
            {
                len += iguana_rwvarint32(1,&serialized[len],(uint32_t *)&siglen);
                if ( siglen > 0 && siglen <= sizeof(cp->sigs[bestk]) )
                {
                    memcpy(&serialized[len],sig,siglen);
                    len += siglen;
                    numsigs++;
                } //else printf("%s -> %s src_or_dest.%d Missing sig from k.%d\n",bp->srccoin->symbol,bp->destcoin->symbol,src_or_dest,k);
            } else serialized[len++] = 0; // usesigs=0 -> insert scriptlen = 0

            len += iguana_rwnum(1,&serialized[len],sizeof(sequenceid),&sequenceid);
            //printf("height.%d mod.%d VINI.%d <- i.%d j.%d\n",height,height % numnotaries,m,i,j);
            m++;
            if ( m == bp->minsigs )//&& k == bestk )
                break;
        }
    }
	// -- vins --
    if ( (n= dpow_voutstandard(myinfo,bp,&serialized[len],m,src_or_dest,pubkeys,numratified)) < 0 )
    {
        printf("error dpow_voutstandard m.%d src_or_dest.%d\n",m,src_or_dest);
        return(zero);
    }
    len += n;

	if (coin->sapling != 0) {
		uint32_t nExpiryHeight = 0;
		uint64_t valueBalance = 0;
		uint8_t nShieldedSpend = 0;
		uint8_t nShieldedOutput = 0;
		uint8_t nJoinSplit = 0;
		len += iguana_rwnum(1, &serialized[len], sizeof(nExpiryHeight), &nExpiryHeight);
		len += iguana_rwnum(1, &serialized[len], sizeof(valueBalance), &valueBalance);
		len += iguana_rwnum(1, &serialized[len], sizeof(nShieldedSpend), &nShieldedSpend); // The number of Spend descriptions in vShieldedSpend
		len += iguana_rwnum(1, &serialized[len], sizeof(nShieldedOutput), &nShieldedOutput); // The number of Output descriptions in vShieldedOutput
		len += iguana_rwnum(1, &serialized[len], sizeof(nJoinSplit), &nJoinSplit); // The number of JoinSplit descriptions in vJoinSplit
	}

	// here if usesigs=0 we have unsigned tx (not preimage), if usesigs=1 - we have signed tx with sigs from nn_bus network (?)

    init_hexbytes_noT(signedtx,serialized,len);
    //printf("[Decker] dpow_notarytx: signedtx.(%s)\n", signedtx);

    //printf("notarytx.(%s) opretlen.%d\n",signedtx,opretlen);
    if ( usesigs == 0 && bestk >= 0 )
    {
        crcval = calc_crc32(0,bp->ratifyrawtx[src_or_dest],bp->rawratifiedlens[src_or_dest]);
        if ( crcval != bp->pendingcrcs[src_or_dest] )
        {
            printf("new crcval.[%d] %x != %x\n",src_or_dest,crcval,bp->pendingcrcs[src_or_dest]);
            bp->pendingcrcs[src_or_dest] = crcval;
        }
        bp->notaries[bp->myind].pendingcrcs[src_or_dest] = bp->pendingcrcs[src_or_dest];
    }
    *numsigsp = numsigs;
    return(bits256_doublesha256(0,serialized,len));
}

cJSON *dpow_vins(struct iguana_info *coin,struct dpow_block *bp,int8_t bestk,uint64_t bestmask,int32_t usesigs,int32_t src_or_dest,int32_t useratified)
{
    int32_t k,m; bits256 txid; uint16_t vout; uint8_t script[35]; char scriptstr[256]; cJSON *vins=0,*item; struct dpow_entry *ep; struct dpow_coinentry *cp;
    vins = cJSON_CreateArray();
    for (m=k=0; k<bp->numnotaries; k++)
    {
        if ( ((1LL << k) & bestmask) != 0 )
        {
            ep = &bp->notaries[k];
            if ( useratified != 0 )
            {
                if ( src_or_dest != 0 )
                {
                    txid = bp->notaries[k].ratifydestutxo;
                    vout = bp->notaries[k].ratifydestvout;
                }
                else
                {
                    txid = bp->notaries[k].ratifysrcutxo;
                    vout = bp->notaries[k].ratifysrcvout;
                }
            }
            else
            {
                cp = (src_or_dest != 0) ? &bp->notaries[k].dest : &bp->notaries[k].src;
                txid = cp->prev_hash;
                vout = cp->prev_vout;
            }
            if ( bits256_nonz(txid) != 0 )
            {
                item = cJSON_CreateObject();
                jaddbits256(item,"txid",txid);
                jaddnum(item,"vout",vout);
				jaddnum(item, "amount", dstr(dpow_utxosize(coin->symbol)));
                if ( k == 0 && bp->require0 != 0 )
                {
                    script[0] = 0x76;
                    script[1] = 0xa9;
                    script[2] = 0x14;
                    calc_rmd160_sha256(&script[3],ep->pubkey,33); // 8ee61a3161993f4f7b7081259bf5f3322d65d3f8
                    script[23] = 0x88;
                    script[24] = 0xac;
                    init_hexbytes_noT(scriptstr,script,25);
                    /*int32_t z;
                    for (z=0; z<25; z++)
                        printf("%02x",script[z]);
                    printf(" <- script0\n");*/
                }
                else
                {
                    script[0] = 33;
                    memcpy(script+1,ep->pubkey,33);
                    script[34] = CHECKSIG;
                    init_hexbytes_noT(scriptstr,script,35);
                }
                jaddstr(item,"scriptPubKey",scriptstr);
                jaddi(vins,item);
                //printf("height.%d mod.%d VINI.%d <- i.%d j.%d\n",height,height % numnotaries,m,i,j);
                m++;
                if ( m == bp->minsigs )//&& k == bestk )
                    break;
            }
            else
            {
                printf("null txid slot k.%d m.%d minsigs.%d\n",k,m,bp->minsigs);
                free_json(vins);
                return(0);
            }
        }
    }
    return(vins);
}

void dpow_rawtxsign(struct supernet_info *myinfo,struct dpow_info *dp,struct iguana_info *coin,struct dpow_block *bp,char *rawtx,cJSON *vins,int8_t bestk,uint64_t bestmask,int32_t myind,int32_t src_or_dest)
{
    int32_t j,m=0,valid,retval=-1; char *jsonstr,*signedtx,*rawtx2,*sigstr,*pubstr; cJSON *signobj,*vinitem,*sobj,*txobj2,*item,*vin; uint8_t pubkey33[33]; bits256 srchash; struct dpow_entry *ep; struct dpow_coinentry *cp;
    if ( bestk < 0 )
        return;
    for (j=0; j<sizeof(srchash); j++)
        srchash.bytes[j] = dp->minerkey33[j+1];
    memset(srchash.bytes,0,sizeof(srchash));
    m = 0;
    ep = &bp->notaries[myind];
    cp = (src_or_dest != 0) ? &bp->notaries[myind].dest : &bp->notaries[myind].src;
    if ( (jsonstr= dpow_signrawtransaction(myinfo,coin,rawtx,vins)) != 0 )
    {
        if ( (signobj= cJSON_Parse(jsonstr)) != 0 )
        {
            if ( ((signedtx= jstr(signobj,"hex")) != 0 || (signedtx= jstr(signobj,"result")) != 0) && (rawtx2= dpow_decoderawtransaction(myinfo,coin,signedtx)) != 0 )
            {
                if ( (txobj2= cJSON_Parse(rawtx2)) != 0 )
                {
                    if ( (vin= jarray(&m,txobj2,"vin")) != 0 )
                    {
                        for (j=0; j<m; j++)
                        {
                            item = jitem(vin,j);
                            vinitem = jitem(vins,j);
                            if ( (sobj= jobj(item,"scriptSig")) != 0 && (sigstr= jstr(sobj,"hex")) != 0 && strlen(sigstr) > 32 )
                            {
                                valid = 0;
                                if ( dp->ratifying != 0 && j == 0 && bp->myind == 0 )
                                    valid = 1;
                                else if ( (pubstr= jstr(vinitem,"scriptPubKey")) != 0 && is_hexstr(pubstr,0) == 70 )
                                {
                                    decode_hex(pubkey33,33,&pubstr[2]);
                                    if ( memcmp(pubkey33,dp->minerkey33,33) == 0 )
                                        valid = 1;
                                    else
                                    {
                                        int32_t z;
                                        for (z=0; z<33; z++)
                                            printf("%02x",dp->minerkey33[z]);
                                        printf(" minerkey33 doesnt match\n");
                                        for (z=0; z<33; z++)
                                            printf("%02x",pubkey33[z]);
                                        printf(" scriptPubKey\n");
                                    }
                                }
                                if ( valid != 0 )
                                {
                                    /*char *txinfo = jprint(item,0);
                                    printf("bestk.%d %llx %s height.%d mod.%d VINI.%d myind.%d MINE j.%d\n",bestk,(long long)bestmask,(src_or_dest != 0) ? bp->destcoin->symbol : bp->srccoin->symbol,bp->height,DPOW_MODIND(bp,0),j,myind,txinfo,j);
                                    free(txinfo); */
                                    cp->siglens[bestk] = (int32_t)strlen(sigstr) >> 1;
                                    if ( src_or_dest != 0 )
                                        bp->destsigsmasks[bestk] |= (1LL << myind);
                                    else bp->srcsigsmasks[bestk] |= (1LL << myind);
                                    decode_hex(cp->sigs[bestk],cp->siglens[bestk],sigstr);
                                    ep->masks[src_or_dest][bestk] = bestmask;
                                    ep->beacon = bp->beacon;
                                    dpow_sigsend(myinfo,dp,bp,myind,bestk,bestmask,srchash,src_or_dest != 0 ? DPOW_SIGBTCCHANNEL : DPOW_SIGCHANNEL);
                                    retval = 0;
                                    break;
                                } else printf("sig.%d of %d didnt match pubkey? (%s)\n",j,m,jprint(vinitem,0));
                            } //else printf("notmine.(%s)\n",jprint(item,0));
                        }
                    } else printf("no vin[] (%s)\n",jprint(txobj2,0));
                    free_json(txobj2);
                } else printf("cant parse.(%s)\n",rawtx2);
                free(rawtx2);
            } else printf("error decoding (%s) %s\n",signedtx==0?"":signedtx,jsonstr);
            free_json(signobj);
        } else printf("error parsing.(%s)\n",jsonstr);
        free(jsonstr);
    } else printf("%s null signature in dpow_rawtxsign\n",dp->symbol);
}

int32_t dpow_signedtxgen(struct supernet_info *myinfo,struct dpow_info *dp,struct iguana_info *coin,struct dpow_block *bp,int8_t bestk,uint64_t bestmask,int32_t myind,uint32_t deprec,int32_t src_or_dest,int32_t useratified)
{
    int32_t j,m,numsigs,len,siglen,retval=-1; char rawtx[32768],*jsonstr,*rawtx2,*signedtx,*sigstr; cJSON *item,*sobj,*vins,*vin,*txobj2,*signobj; bits256 txid,srchash,zero; struct dpow_entry *ep;
    ep = &bp->notaries[myind];
    memset(&zero,0,sizeof(zero));
    if ( bestk < 0 )
        return(-1);
    for (j=0; j<sizeof(srchash); j++)
        srchash.bytes[j] = dp->minerkey33[j+1];
    if ( (vins= dpow_vins(coin,bp,bestk,bestmask,1,src_or_dest,useratified)) != 0 )
    {
        txid = dpow_notarytx(myinfo,rawtx,&numsigs,coin->chain->isPoS,bp,bestk,bestmask,0,src_or_dest,bp->numratified!=0?bp->ratified_pubkeys:0,useratified*bp->numratified);
        //char str[65]; printf("%s signedtxgen %s src_or_dest.%d (%d %llx) useratified.%d raw.(%s)\n",dp->symbol,bits256_str(str,txid),src_or_dest,bestk,(long long)bestmask,useratified,rawtx);
        if ( bits256_nonz(txid) != 0 && rawtx[0] != 0 ) // send tx to share utxo set
        {
            if ( useratified != 0 )
            {
                len = (int32_t)strlen(rawtx) >> 1;
                if ( len <= sizeof(bp->ratifyrawtx[src_or_dest]) )
                {
                    decode_hex(bp->ratifyrawtx[src_or_dest],len,rawtx),bp->rawratifiedlens[src_or_dest] = len;
                }
                if ( (jsonstr= dpow_signrawtransaction(myinfo,coin,rawtx,vins)) != 0 )
                {
                    if ( (signobj= cJSON_Parse(jsonstr)) != 0 )
                    {
                        if ( ((signedtx= jstr(signobj,"hex")) != 0 || (signedtx= jstr(signobj,"result")) != 0) && (rawtx2= dpow_decoderawtransaction(myinfo,coin,signedtx)) != 0 )
                        {
                            if ( (txobj2= cJSON_Parse(rawtx2)) != 0 )
                            {
                                if ( (vin= jarray(&m,txobj2,"vin")) != 0 )
                                {
                                    for (j=0; j<m; j++)
                                    {
                                        item = jitem(vin,j);
                                        if ( (sobj= jobj(item,"scriptSig")) != 0 && (sigstr= jstr(sobj,"hex")) != 0 && strlen(sigstr) > 32 )
                                        {
                                            siglen = (int32_t)strlen(sigstr) >> 1;
                                            bp->ratifysiglens[src_or_dest] = siglen;
                                            decode_hex(bp->ratifysigs[src_or_dest],siglen,sigstr);
                                            bp->notaries[bp->myind].ratifysiglens[src_or_dest] = siglen;
                                            memcpy(bp->notaries[bp->myind].ratifysigs[src_or_dest],bp->ratifysigs[src_or_dest],siglen);
                                            bp->ratifysigmasks[src_or_dest] |= (1LL << bp->myind);
                                            printf("RATIFYSIG[%d] <- set notaryid.%d siglen.%d (%s).%d\n",src_or_dest,bp->myind,bp->ratifysiglens[src_or_dest],sigstr,siglen);
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                else
                {
                    printf("signrawtransaction error vins.(%s) rawtx.(%s)\n",jprint(vins,0),rawtx);
                }
            } else dpow_rawtxsign(myinfo,dp,coin,bp,rawtx,vins,bestk,bestmask,myind,src_or_dest);
        } else printf("signedtxgen zero txid or null rawtx\n");
        free_json(vins);
    }
    else if ( (bestmask & bp->recvmask) != bestmask )
        printf("signedtxgen error generating vins bestk.%d %llx recv.%llx need to recv %llx\n",bestk,(long long)bestmask,(long long)bp->recvmask,(long long)(bestmask & ~bp->recvmask));
    return(retval);
}

uint64_t iguana_fastnotariescount(struct supernet_info *myinfo, struct dpow_info *dp, struct dpow_block *bp, int32_t src_or_dest, int32_t *checkall);

void dpow_sigscheck(struct supernet_info *myinfo,struct dpow_info *dp,struct dpow_block *bp,int32_t myind,int32_t src_or_dest,int8_t bestk,uint64_t bestmask,uint8_t pubkeys[64][33],int32_t numratified)
{
    bits256 txid,srchash,zero,signedtxid; struct iguana_info *coin; int32_t i,j,len,numsigs,buflen=0,errorcode=0; char *retstr=0,str[65],str2[65],printstr[65536]; uint8_t txdata[32768]; uint32_t state; uint64_t testbestmask;
    coin = (src_or_dest != 0) ? bp->destcoin : bp->srccoin;
    memset(zero.bytes,0,sizeof(zero));
    memset(txid.bytes,0,sizeof(txid));
    if ( bestk >= 0 && bp->state != 0xffffffff && coin != 0 )
    {
        dpow_notarytx(myinfo,bp->signedtx,&numsigs,coin->chain->isPoS,bp,bestk,bestmask,0,src_or_dest,pubkeys,numratified); // setcrcval
        signedtxid = dpow_notarytx(myinfo,bp->signedtx,&numsigs,coin->chain->isPoS,bp,bestk,bestmask,1,src_or_dest,pubkeys,numratified);
        bp->state = 1;
        if ( bits256_nonz(signedtxid) != 0 && numsigs == bp->minsigs ) 
        {
            if ( (retstr= dpow_sendrawtransaction(myinfo,coin,bp->signedtx,(bestmask & (1LL << bp->myind)),&errorcode)) != 0 )
            {
                //printf("sendrawtransaction.(%s)\n",retstr);
                if ( is_hexstr(retstr,0) == sizeof(txid)*2 )
                {
                    decode_hex(txid.bytes,sizeof(txid),retstr);
                    if ( bits256_cmp(txid,signedtxid) == 0 )
                    {
                        if ( src_or_dest != 0 )
                        {
                            bp->desttxid = txid;
                            dpow_signedtxgen(myinfo,dp,bp->srccoin,bp,bestk,bestmask,myind,DPOW_SIGCHANNEL,0,numratified != 0);
                            
                        } else 
                        {
                            bp->srctxid = txid;
                        }
                        len = (int32_t)strlen(bp->signedtx) >> 1;
                        decode_hex(txdata+32,len,bp->signedtx);
                        for (j=0; j<sizeof(srchash); j++)
                            txdata[j] = txid.bytes[j];
                        state = src_or_dest != 0 ? 1000 : 0xffffffff;
                        if ( bp->state < state )
                        {
                            bp->state = state;
                            dpow_send(myinfo,dp,bp,txid,bp->hashmsg,(src_or_dest != 0) ? DPOW_BTCTXIDCHANNEL : DPOW_TXIDCHANNEL,bp->height,txdata,len+32);
                            //printf("complete statemachine.%s ht.%d state.%d (%x %x)\n",coin->symbol,bp->height,bp->state,bp->hashmsg.uints[0],txid.uints[0]);
                            if ( src_or_dest == 0 )
                            {
                                portable_mutex_lock(&dp->dpmutex);
                                dp->lastnotarized = bp->hashmsg;
                                dp->lastnotarizedht = bp->height;
                                dp->lastrecvmask = bp->recvmask;
                                dp->prevDESTHEIGHT = bp->pendingprevDESTHT;
                                dp->prevnotatxid = bp->desttxid;
                                dp->bestks[dp->numbestks] = bp->bestk;
                                dp->numbestks = (dp->numbestks==64 ? 0 : dp->numbestks+1);
                                portable_mutex_unlock(&dp->dpmutex);
                            }
                        }
                    } else printf("sendtxid mismatch got %s instead of %s\n",bits256_str(str,txid),bits256_str(str2,signedtxid));
                }
            } // else printf("NULL return from sendrawtransaction. abort\n");
            
            // If the error code is -27, it means the tx has been confirmed before this node tried to send it, ignore it.
            // Any other case is a failed transaction, so extract as much information as we can and print to logs. 
            if ( errorcode != 0 && errorcode != -27 )
            {
                buflen = sprintf(printstr, RED"dpow_sigscheck: [%s:%i] coin.%s errcode.%i sapling.%i "RESET,bp->srccoin->symbol,bp->height,coin->symbol,errorcode,coin->sapling);
                // for non sapling coins/utxos we need a diffrent notary count/sigcheck
                int32_t checkall = 0;
                if ( coin->sapling != 0 && (testbestmask= iguana_fastnotariescount(myinfo, dp, bp, src_or_dest, &checkall)) != bp->bestmask && checkall >= 0 )
                {
                    checkall++;
                    uint64_t failedmask = bp->bestmask^testbestmask;
                    buflen += sprintf(printstr+buflen,RED"failedbestmask.%llx bestmask.%llx\n"RESET,(long long)failedmask,(long long)bp->bestmask);
                    buflen += sprintf(printstr+buflen,">>> tx.%s\n",bp->signedtx);
                    uint64_t testmask = iguana_fastnotariescount(myinfo, dp, bp, src_or_dest, &checkall);
                    if ( checkall > 0 )
                    {
                        buflen += sprintf(printstr+buflen," >>> signed by: ");
                        for (i=0; i<bp->numnotaries; i++)
                            if ( ((1LL << i) & testmask) != 0 )
                                buflen += sprintf(printstr+buflen,"%s, ",Notaries_elected[i][0]);
                        buflen += sprintf(printstr+buflen,"\n >>> missing sigs: ");
                        for (j=0; j<bp->numnotaries; j++)
                            if ( (failedmask & (1LL << j)) != 0 )
                            {
                                buflen += sprintf(printstr+buflen,"%s, ", Notaries_elected[j][0]);
                                //dp->lastbanheight[j] = bp->height; // disable ban, causes bugs, monitoring via logs should be enough.
                            }
                        buflen += sprintf(printstr+buflen,"\n");
                    }
                }
                buflen += sprintf(printstr+buflen," >>> inputs spent: \n");
                for (j=0; j<bp->numnotaries; j++)
                {
                    if ( ((1LL << j) & bp->bestmask) != 0 )
                    {
                        if ( src_or_dest != 0 )
                        {
                            if ( dpow_gettxout(myinfo, bp->destcoin, bp->notaries[j].dest.prev_hash, bp->notaries[j].dest.prev_vout) == 0 ) 
                                buflen += sprintf(printstr+buflen,"    [%s] txid.%s v.%i \n", Notaries_elected[j][0],bits256_str(str,bp->notaries[j].dest.prev_hash), bp->notaries[j].dest.prev_vout);
                        }
                        else
                        {
                            if ( dpow_gettxout(myinfo, bp->srccoin, bp->notaries[j].src.prev_hash, bp->notaries[j].src.prev_vout) == 0 ) 
                                buflen += sprintf(printstr+buflen,"    [%s] txid.%s v.%i \n", Notaries_elected[j][0],bits256_str(str,bp->notaries[j].src.prev_hash), bp->notaries[j].src.prev_vout);
                        }
                    }
                }
                buflen += sprintf(printstr+buflen,"\n\n");
#ifdef LOGTX
                FILE * fptr;
                fptr = fopen("failed_notarizations", "a+");
                fprintf(fptr, "%s",printstr);
                fclose(fptr);
#endif 
                printf("%s",printstr);
                bp->state = 0xffffffff;
            }
        } //else printf("numsigs.%d vs required.%d\n",numsigs,bp->minsigs);
    }
}
