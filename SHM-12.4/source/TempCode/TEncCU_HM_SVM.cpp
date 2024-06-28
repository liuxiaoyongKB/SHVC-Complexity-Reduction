/** finish encoding a cu and handle end-of-slice conditions
 * \param pcCU
 * \param uiAbsPartIdx
 * \param uiDepth
 * \returns Void
 */
Void TEncCu::finishCU(TComDataCU* pcCU, UInt uiAbsPartIdx)
{
	TComPic* pcPic = pcCU->getPic();
	TComSlice * pcSlice = pcCU->getPic()->getSlice(pcCU->getPic()->getCurrSliceIdx());

	//Calculate end address
	const Int  currentCTUTsAddr = pcPic->getPicSym()->getCtuRsToTsAddrMap(pcCU->getCtuRsAddr());
	const Bool isLastSubCUOfCtu = pcCU->isLastSubCUOfCtu(uiAbsPartIdx);
	if (isLastSubCUOfCtu)
	{
		// The 1-terminating bit is added to all streams, so don't add it here when it's 1.
		// i.e. when the slice segment CurEnd CTU address is the current CTU address+1.
		if (pcSlice->getSliceSegmentCurEndCtuTsAddr() != currentCTUTsAddr + 1)
		{
			m_pcEntropyCoder->encodeTerminatingBit(0);
		}
	}
}

/** Compute QP for each CU
 * \param pcCU Target CU
 * \param uiDepth CU depth
 * \returns quantization parameter
 */
Int TEncCu::xComputeQP(TComDataCU* pcCU, UInt uiDepth)
{
	Int iBaseQp = pcCU->getSlice()->getSliceQp();
	Int iQpOffset = 0;
	if (m_pcEncCfg->getUseAdaptiveQP())
	{
		TEncPic* pcEPic = dynamic_cast<TEncPic*>(pcCU->getPic());
		UInt uiAQDepth = min(uiDepth, pcEPic->getMaxAQDepth() - 1);
		TEncPicQPAdaptationLayer* pcAQLayer = pcEPic->getAQLayer(uiAQDepth);
		UInt uiAQUPosX = pcCU->getCUPelX() / pcAQLayer->getAQPartWidth();
		UInt uiAQUPosY = pcCU->getCUPelY() / pcAQLayer->getAQPartHeight();
		UInt uiAQUStride = pcAQLayer->getAQPartStride();
		TEncQPAdaptationUnit* acAQU = pcAQLayer->getQPAdaptationUnit();

		Double dMaxQScale = pow(2.0, m_pcEncCfg->getQPAdaptationRange() / 6.0);
		Double dAvgAct = pcAQLayer->getAvgActivity();
		Double dCUAct = acAQU[uiAQUPosY * uiAQUStride + uiAQUPosX].getActivity();
		Double dNormAct = (dMaxQScale*dCUAct + dAvgAct) / (dCUAct + dMaxQScale * dAvgAct);
		Double dQpOffset = log(dNormAct) / log(2.0) * 6.0;
		iQpOffset = Int(floor(dQpOffset + 0.49999));
	}

	return Clip3(-pcCU->getSlice()->getSPS()->getQpBDOffset(CHANNEL_TYPE_LUMA), MAX_QP, iBaseQp + iQpOffset);
}

/** encode a CU block recursively
 * \param pcCU
 * \param uiAbsPartIdx
 * \param uiDepth
 * \returns Void
 */
Void TEncCu::xEncodeCU(TComDataCU* pcCU, UInt uiAbsPartIdx, UInt uiDepth)
{
	TComPic   *const pcPic = pcCU->getPic();
	TComSlice *const pcSlice = pcCU->getSlice();
	const TComSPS   &sps = *(pcSlice->getSPS());
	const TComPPS   &pps = *(pcSlice->getPPS());

	const UInt maxCUWidth = sps.getMaxCUWidth();
	const UInt maxCUHeight = sps.getMaxCUHeight();

	Bool bBoundary = false;
	UInt uiLPelX = pcCU->getCUPelX() + g_auiRasterToPelX[g_auiZscanToRaster[uiAbsPartIdx]];
	const UInt uiRPelX = uiLPelX + (maxCUWidth >> uiDepth) - 1;
	UInt uiTPelY = pcCU->getCUPelY() + g_auiRasterToPelY[g_auiZscanToRaster[uiAbsPartIdx]];
	const UInt uiBPelY = uiTPelY + (maxCUHeight >> uiDepth) - 1;

	if ((uiRPelX < sps.getPicWidthInLumaSamples()) && (uiBPelY < sps.getPicHeightInLumaSamples()))
	{
		m_pcEntropyCoder->encodeSplitFlag(pcCU, uiAbsPartIdx, uiDepth);
	}
	else
	{
		bBoundary = true;
	}

	if (((uiDepth < pcCU->getDepth(uiAbsPartIdx)) && (uiDepth < sps.getLog2DiffMaxMinCodingBlockSize())) || bBoundary)
	{
		UInt uiQNumParts = (pcPic->getNumPartitionsInCtu() >> (uiDepth << 1)) >> 2;
		if (uiDepth == pps.getMaxCuDQPDepth() && pps.getUseDQP())
		{
			setdQPFlag(true);
		}

		if (uiDepth == pps.getPpsRangeExtension().getDiffCuChromaQpOffsetDepth() && pcSlice->getUseChromaQpAdj())
		{
			setCodeChromaQpAdjFlag(true);
		}

		for (UInt uiPartUnitIdx = 0; uiPartUnitIdx < 4; uiPartUnitIdx++, uiAbsPartIdx += uiQNumParts)
		{
			uiLPelX = pcCU->getCUPelX() + g_auiRasterToPelX[g_auiZscanToRaster[uiAbsPartIdx]];
			uiTPelY = pcCU->getCUPelY() + g_auiRasterToPelY[g_auiZscanToRaster[uiAbsPartIdx]];
			if ((uiLPelX < sps.getPicWidthInLumaSamples()) && (uiTPelY < sps.getPicHeightInLumaSamples()))
			{
				xEncodeCU(pcCU, uiAbsPartIdx, uiDepth + 1);
			}
		}
		return;
	}

	if (uiDepth <= pps.getMaxCuDQPDepth() && pps.getUseDQP())
	{
		setdQPFlag(true);
	}

	if (uiDepth <= pps.getPpsRangeExtension().getDiffCuChromaQpOffsetDepth() && pcSlice->getUseChromaQpAdj())
	{
		setCodeChromaQpAdjFlag(true);
	}

	if (pps.getTransquantBypassEnableFlag())
	{
		m_pcEntropyCoder->encodeCUTransquantBypassFlag(pcCU, uiAbsPartIdx);
	}

	if (!pcSlice->isIntra())
	{
		m_pcEntropyCoder->encodeSkipFlag(pcCU, uiAbsPartIdx);
	}

	if (pcCU->isSkipped(uiAbsPartIdx))
	{
		m_pcEntropyCoder->encodeMergeIndex(pcCU, uiAbsPartIdx);
		finishCU(pcCU, uiAbsPartIdx);
		return;
	}

	m_pcEntropyCoder->encodePredMode(pcCU, uiAbsPartIdx);
	m_pcEntropyCoder->encodePartSize(pcCU, uiAbsPartIdx, uiDepth);

	if (pcCU->isIntra(uiAbsPartIdx) && pcCU->getPartitionSize(uiAbsPartIdx) == SIZE_2Nx2N)
	{
		m_pcEntropyCoder->encodeIPCMInfo(pcCU, uiAbsPartIdx);

		if (pcCU->getIPCMFlag(uiAbsPartIdx))
		{
			// Encode slice finish
			finishCU(pcCU, uiAbsPartIdx);
			return;
		}
	}

	// prediction Info ( Intra : direction mode, Inter : Mv, reference idx )
	m_pcEntropyCoder->encodePredInfo(pcCU, uiAbsPartIdx);

	// Encode Coefficients
	Bool bCodeDQP = getdQPFlag();
	Bool codeChromaQpAdj = getCodeChromaQpAdjFlag();
	m_pcEntropyCoder->encodeCoeff(pcCU, uiAbsPartIdx, uiDepth, bCodeDQP, codeChromaQpAdj);
	setCodeChromaQpAdjFlag(codeChromaQpAdj);
	setdQPFlag(bCodeDQP);

	// --- write terminating bit ---
	finishCU(pcCU, uiAbsPartIdx);
}

Int xCalcHADs8x8_ISlice(Pel *piOrg, Int iStrideOrg)
{
	Int k, i, j, jj;
	Int diff[64], m1[8][8], m2[8][8], m3[8][8], iSumHad = 0;

	for (k = 0; k < 64; k += 8)
	{
		diff[k + 0] = piOrg[0];
		diff[k + 1] = piOrg[1];
		diff[k + 2] = piOrg[2];
		diff[k + 3] = piOrg[3];
		diff[k + 4] = piOrg[4];
		diff[k + 5] = piOrg[5];
		diff[k + 6] = piOrg[6];
		diff[k + 7] = piOrg[7];

		piOrg += iStrideOrg;
	}

	//horizontal
	for (j = 0; j < 8; j++)
	{
		jj = j << 3;
		m2[j][0] = diff[jj] + diff[jj + 4];
		m2[j][1] = diff[jj + 1] + diff[jj + 5];
		m2[j][2] = diff[jj + 2] + diff[jj + 6];
		m2[j][3] = diff[jj + 3] + diff[jj + 7];
		m2[j][4] = diff[jj] - diff[jj + 4];
		m2[j][5] = diff[jj + 1] - diff[jj + 5];
		m2[j][6] = diff[jj + 2] - diff[jj + 6];
		m2[j][7] = diff[jj + 3] - diff[jj + 7];

		m1[j][0] = m2[j][0] + m2[j][2];
		m1[j][1] = m2[j][1] + m2[j][3];
		m1[j][2] = m2[j][0] - m2[j][2];
		m1[j][3] = m2[j][1] - m2[j][3];
		m1[j][4] = m2[j][4] + m2[j][6];
		m1[j][5] = m2[j][5] + m2[j][7];
		m1[j][6] = m2[j][4] - m2[j][6];
		m1[j][7] = m2[j][5] - m2[j][7];

		m2[j][0] = m1[j][0] + m1[j][1];
		m2[j][1] = m1[j][0] - m1[j][1];
		m2[j][2] = m1[j][2] + m1[j][3];
		m2[j][3] = m1[j][2] - m1[j][3];
		m2[j][4] = m1[j][4] + m1[j][5];
		m2[j][5] = m1[j][4] - m1[j][5];
		m2[j][6] = m1[j][6] + m1[j][7];
		m2[j][7] = m1[j][6] - m1[j][7];
	}

	//vertical
	for (i = 0; i < 8; i++)
	{
		m3[0][i] = m2[0][i] + m2[4][i];
		m3[1][i] = m2[1][i] + m2[5][i];
		m3[2][i] = m2[2][i] + m2[6][i];
		m3[3][i] = m2[3][i] + m2[7][i];
		m3[4][i] = m2[0][i] - m2[4][i];
		m3[5][i] = m2[1][i] - m2[5][i];
		m3[6][i] = m2[2][i] - m2[6][i];
		m3[7][i] = m2[3][i] - m2[7][i];

		m1[0][i] = m3[0][i] + m3[2][i];
		m1[1][i] = m3[1][i] + m3[3][i];
		m1[2][i] = m3[0][i] - m3[2][i];
		m1[3][i] = m3[1][i] - m3[3][i];
		m1[4][i] = m3[4][i] + m3[6][i];
		m1[5][i] = m3[5][i] + m3[7][i];
		m1[6][i] = m3[4][i] - m3[6][i];
		m1[7][i] = m3[5][i] - m3[7][i];

		m2[0][i] = m1[0][i] + m1[1][i];
		m2[1][i] = m1[0][i] - m1[1][i];
		m2[2][i] = m1[2][i] + m1[3][i];
		m2[3][i] = m1[2][i] - m1[3][i];
		m2[4][i] = m1[4][i] + m1[5][i];
		m2[5][i] = m1[4][i] - m1[5][i];
		m2[6][i] = m1[6][i] + m1[7][i];
		m2[7][i] = m1[6][i] - m1[7][i];
	}

	for (i = 0; i < 8; i++)
	{
		for (j = 0; j < 8; j++)
		{
			iSumHad += abs(m2[i][j]);
		}
	}
	iSumHad -= abs(m2[0][0]);
	iSumHad = (iSumHad + 2) >> 2;
	return(iSumHad);
}

Int  TEncCu::updateCtuDataISlice(TComDataCU* pCtu, Int width, Int height)
{
	Int  xBl, yBl;
	const Int iBlkSize = 8;

	Pel* pOrgInit = pCtu->getPic()->getPicYuvOrg()->getAddr(COMPONENT_Y, pCtu->getCtuRsAddr(), 0);
	Int  iStrideOrig = pCtu->getPic()->getPicYuvOrg()->getStride(COMPONENT_Y);
	Pel  *pOrg;

	Int iSumHad = 0;
	for (yBl = 0; (yBl + iBlkSize) <= height; yBl += iBlkSize)
	{
		for (xBl = 0; (xBl + iBlkSize) <= width; xBl += iBlkSize)
		{
			pOrg = pOrgInit + iStrideOrig * yBl + xBl;
			iSumHad += xCalcHADs8x8_ISlice(pOrg, iStrideOrig);
		}
	}
	return(iSumHad);
}

/** check RD costs for a CU block encoded with merge
 * \param rpcBestCU
 * \param rpcTempCU
 * \param earlyDetectionSkipMode
 */
Void TEncCu::xCheckRDCostMerge2Nx2N(TComDataCU*& rpcBestCU, TComDataCU*& rpcTempCU DEBUG_STRING_FN_DECLARE(sDebug), Bool *earlyDetectionSkipMode)
{
	assert(rpcTempCU->getSlice()->getSliceType() != I_SLICE);
	TComMvField  cMvFieldNeighbours[2 * MRG_MAX_NUM_CANDS]; // double length for mv of both lists
	UChar uhInterDirNeighbours[MRG_MAX_NUM_CANDS];
	Int numValidMergeCand = 0;
	const Bool bTransquantBypassFlag = rpcTempCU->getCUTransquantBypass(0);

	for (UInt ui = 0; ui < rpcTempCU->getSlice()->getMaxNumMergeCand(); ++ui)
	{
		uhInterDirNeighbours[ui] = 0;
	}
	UChar uhDepth = rpcTempCU->getDepth(0);
	rpcTempCU->setPartSizeSubParts(SIZE_2Nx2N, 0, uhDepth); // interprets depth relative to CTU level
	rpcTempCU->getInterMergeCandidates(0, 0, cMvFieldNeighbours, uhInterDirNeighbours, numValidMergeCand);

	Int mergeCandBuffer[MRG_MAX_NUM_CANDS];
	for (UInt ui = 0; ui < numValidMergeCand; ++ui)
	{
		mergeCandBuffer[ui] = 0;
	}

	Bool bestIsSkip = false;

	UInt iteration;
	if (rpcTempCU->isLosslessCoded(0))
	{
		iteration = 1;
	}
	else
	{
		iteration = 2;
	}
	DEBUG_STRING_NEW(bestStr)

		for (UInt uiNoResidual = 0; uiNoResidual < iteration; ++uiNoResidual)
		{
			for (UInt uiMergeCand = 0; uiMergeCand < numValidMergeCand; ++uiMergeCand)
			{
				if (!(uiNoResidual == 1 && mergeCandBuffer[uiMergeCand] == 1))
				{
					if (!(bestIsSkip && uiNoResidual == 0))
					{
						DEBUG_STRING_NEW(tmpStr)
							// set MC parameters
							rpcTempCU->setPredModeSubParts(MODE_INTER, 0, uhDepth); // interprets depth relative to CTU level
						rpcTempCU->setCUTransquantBypassSubParts(bTransquantBypassFlag, 0, uhDepth);
						rpcTempCU->setChromaQpAdjSubParts(bTransquantBypassFlag ? 0 : m_cuChromaQpOffsetIdxPlus1, 0, uhDepth);
						rpcTempCU->setPartSizeSubParts(SIZE_2Nx2N, 0, uhDepth); // interprets depth relative to CTU level
						rpcTempCU->setMergeFlagSubParts(true, 0, 0, uhDepth); // interprets depth relative to CTU level
						rpcTempCU->setMergeIndexSubParts(uiMergeCand, 0, 0, uhDepth); // interprets depth relative to CTU level
						rpcTempCU->setInterDirSubParts(uhInterDirNeighbours[uiMergeCand], 0, 0, uhDepth); // interprets depth relative to CTU level
						rpcTempCU->getCUMvField(REF_PIC_LIST_0)->setAllMvField(cMvFieldNeighbours[0 + 2 * uiMergeCand], SIZE_2Nx2N, 0, 0); // interprets depth relative to rpcTempCU level
						rpcTempCU->getCUMvField(REF_PIC_LIST_1)->setAllMvField(cMvFieldNeighbours[1 + 2 * uiMergeCand], SIZE_2Nx2N, 0, 0); // interprets depth relative to rpcTempCU level

						// do MC
						m_pcPredSearch->motionCompensation(rpcTempCU, m_ppcPredYuvTemp[uhDepth]);
						// estimate residual and encode everything
						m_pcPredSearch->encodeResAndCalcRdInterCU(rpcTempCU,
							m_ppcOrigYuv[uhDepth],
							m_ppcPredYuvTemp[uhDepth],
							m_ppcResiYuvTemp[uhDepth],
							m_ppcResiYuvBest[uhDepth],
							m_ppcRecoYuvTemp[uhDepth],
							(uiNoResidual != 0) DEBUG_STRING_PASS_INTO(tmpStr));

#if DEBUG_STRING
						DebugInterPredResiReco(tmpStr, *(m_ppcPredYuvTemp[uhDepth]), *(m_ppcResiYuvBest[uhDepth]), *(m_ppcRecoYuvTemp[uhDepth]), DebugStringGetPredModeMask(rpcTempCU->getPredictionMode(0)));
#endif

						if ((uiNoResidual == 0) && (rpcTempCU->getQtRootCbf(0) == 0))
						{
							// If no residual when allowing for one, then set mark to not try case where residual is forced to 0
							mergeCandBuffer[uiMergeCand] = 1;
						}

						Int orgQP = rpcTempCU->getQP(0);
						xCheckDQP(rpcTempCU);
						xCheckBestMode(rpcBestCU, rpcTempCU, uhDepth DEBUG_STRING_PASS_INTO(bestStr) DEBUG_STRING_PASS_INTO(tmpStr));

						rpcTempCU->initEstData(uhDepth, orgQP, bTransquantBypassFlag);

						if (m_pcEncCfg->getUseFastDecisionForMerge() && !bestIsSkip)
						{
							bestIsSkip = rpcBestCU->getQtRootCbf(0) == 0;
						}
					}
				}
			}

			if (uiNoResidual == 0 && m_pcEncCfg->getUseEarlySkipDetection())
			{
				if (rpcBestCU->getQtRootCbf(0) == 0)
				{
					if (rpcBestCU->getMergeFlag(0))
					{
						*earlyDetectionSkipMode = true;
					}
					else if (m_pcEncCfg->getFastSearch() != SELECTIVE)
					{
						Int absoulte_MV = 0;
						for (UInt uiRefListIdx = 0; uiRefListIdx < 2; uiRefListIdx++)
						{
							if (rpcBestCU->getSlice()->getNumRefIdx(RefPicList(uiRefListIdx)) > 0)
							{
								TComCUMvField* pcCUMvField = rpcBestCU->getCUMvField(RefPicList(uiRefListIdx));
								Int iHor = pcCUMvField->getMvd(0).getAbsHor();
								Int iVer = pcCUMvField->getMvd(0).getAbsVer();
								absoulte_MV += iHor + iVer;
							}
						}

						if (absoulte_MV == 0)
						{
							*earlyDetectionSkipMode = true;
						}
					}
				}
			}
		}
	DEBUG_STRING_APPEND(sDebug, bestStr)
}


#if AMP_MRG
Void TEncCu::xCheckRDCostInter(TComDataCU*& rpcBestCU, TComDataCU*& rpcTempCU, PartSize ePartSize DEBUG_STRING_FN_DECLARE(sDebug), Bool bUseMRG)
#else
Void TEncCu::xCheckRDCostInter(TComDataCU*& rpcBestCU, TComDataCU*& rpcTempCU, PartSize ePartSize)
#endif
{
	DEBUG_STRING_NEW(sTest)

		// prior to this, rpcTempCU will have just been reset using rpcTempCU->initEstData( uiDepth, iQP, bIsLosslessMode );
		UChar uhDepth = rpcTempCU->getDepth(0);

	rpcTempCU->setPartSizeSubParts(ePartSize, 0, uhDepth);
	rpcTempCU->setPredModeSubParts(MODE_INTER, 0, uhDepth);
	rpcTempCU->setChromaQpAdjSubParts(rpcTempCU->getCUTransquantBypass(0) ? 0 : m_cuChromaQpOffsetIdxPlus1, 0, uhDepth);

#if AMP_MRG
	rpcTempCU->setMergeAMP(true);
	m_pcPredSearch->predInterSearch(rpcTempCU, m_ppcOrigYuv[uhDepth], m_ppcPredYuvTemp[uhDepth], m_ppcResiYuvTemp[uhDepth], m_ppcRecoYuvTemp[uhDepth] DEBUG_STRING_PASS_INTO(sTest), false, bUseMRG);
#else
	m_pcPredSearch->predInterSearch(rpcTempCU, m_ppcOrigYuv[uhDepth], m_ppcPredYuvTemp[uhDepth], m_ppcResiYuvTemp[uhDepth], m_ppcRecoYuvTemp[uhDepth]);
#endif

#if AMP_MRG
	if (!rpcTempCU->getMergeAMP())
	{
		return;
	}
#endif

	m_pcPredSearch->encodeResAndCalcRdInterCU(rpcTempCU, m_ppcOrigYuv[uhDepth], m_ppcPredYuvTemp[uhDepth], m_ppcResiYuvTemp[uhDepth], m_ppcResiYuvBest[uhDepth], m_ppcRecoYuvTemp[uhDepth], false DEBUG_STRING_PASS_INTO(sTest));
	rpcTempCU->getTotalCost() = m_pcRdCost->calcRdCost(rpcTempCU->getTotalBits(), rpcTempCU->getTotalDistortion());

#if DEBUG_STRING
	DebugInterPredResiReco(sTest, *(m_ppcPredYuvTemp[uhDepth]), *(m_ppcResiYuvBest[uhDepth]), *(m_ppcRecoYuvTemp[uhDepth]), DebugStringGetPredModeMask(rpcTempCU->getPredictionMode(0)));
#endif

	xCheckDQP(rpcTempCU);
	xCheckBestMode(rpcBestCU, rpcTempCU, uhDepth DEBUG_STRING_PASS_INTO(sDebug) DEBUG_STRING_PASS_INTO(sTest));
}

Void TEncCu::xCheckRDCostIntra(TComDataCU *&rpcBestCU,
	TComDataCU *&rpcTempCU,
	Double      &cost,
	PartSize     eSize
	DEBUG_STRING_FN_DECLARE(sDebug))
{
	DEBUG_STRING_NEW(sTest)

		UInt uiDepth = rpcTempCU->getDepth(0);

	rpcTempCU->setSkipFlagSubParts(false, 0, uiDepth);

	rpcTempCU->setPartSizeSubParts(eSize, 0, uiDepth);
	rpcTempCU->setPredModeSubParts(MODE_INTRA, 0, uiDepth);
	rpcTempCU->setChromaQpAdjSubParts(rpcTempCU->getCUTransquantBypass(0) ? 0 : m_cuChromaQpOffsetIdxPlus1, 0, uiDepth);

	Pel resiLuma[NUMBER_OF_STORED_RESIDUAL_TYPES][MAX_CU_SIZE * MAX_CU_SIZE];

	m_pcPredSearch->estIntraPredLumaQT(rpcTempCU, m_ppcOrigYuv[uiDepth], m_ppcPredYuvTemp[uiDepth], m_ppcResiYuvTemp[uiDepth], m_ppcRecoYuvTemp[uiDepth], resiLuma DEBUG_STRING_PASS_INTO(sTest));

	m_ppcRecoYuvTemp[uiDepth]->copyToPicComponent(COMPONENT_Y, rpcTempCU->getPic()->getPicYuvRec(), rpcTempCU->getCtuRsAddr(), rpcTempCU->getZorderIdxInCtu());

	if (rpcBestCU->getPic()->getChromaFormat() != CHROMA_400)
	{
		m_pcPredSearch->estIntraPredChromaQT(rpcTempCU, m_ppcOrigYuv[uiDepth], m_ppcPredYuvTemp[uiDepth], m_ppcResiYuvTemp[uiDepth], m_ppcRecoYuvTemp[uiDepth], resiLuma DEBUG_STRING_PASS_INTO(sTest));
	}

	m_pcEntropyCoder->resetBits();

	if (rpcTempCU->getSlice()->getPPS()->getTransquantBypassEnableFlag())
	{
		m_pcEntropyCoder->encodeCUTransquantBypassFlag(rpcTempCU, 0, true);
	}

	m_pcEntropyCoder->encodeSkipFlag(rpcTempCU, 0, true);
	m_pcEntropyCoder->encodePredMode(rpcTempCU, 0, true);
	m_pcEntropyCoder->encodePartSize(rpcTempCU, 0, uiDepth, true);
	m_pcEntropyCoder->encodePredInfo(rpcTempCU, 0);
	m_pcEntropyCoder->encodeIPCMInfo(rpcTempCU, 0, true);

	// Encode Coefficients
	Bool bCodeDQP = getdQPFlag();
	Bool codeChromaQpAdjFlag = getCodeChromaQpAdjFlag();
	m_pcEntropyCoder->encodeCoeff(rpcTempCU, 0, uiDepth, bCodeDQP, codeChromaQpAdjFlag);
	setCodeChromaQpAdjFlag(codeChromaQpAdjFlag);
	setdQPFlag(bCodeDQP);

	m_pcRDGoOnSbacCoder->store(m_pppcRDSbacCoder[uiDepth][CI_TEMP_BEST]);

	rpcTempCU->getTotalBits() = m_pcEntropyCoder->getNumberOfWrittenBits();
	rpcTempCU->getTotalBins() = ((TEncBinCABAC *)((TEncSbac*)m_pcEntropyCoder->m_pcEntropyCoderIf)->getEncBinIf())->getBinsCoded();
	rpcTempCU->getTotalCost() = m_pcRdCost->calcRdCost(rpcTempCU->getTotalBits(), rpcTempCU->getTotalDistortion());

	xCheckDQP(rpcTempCU);

	cost = rpcTempCU->getTotalCost();

	xCheckBestMode(rpcBestCU, rpcTempCU, uiDepth DEBUG_STRING_PASS_INTO(sDebug) DEBUG_STRING_PASS_INTO(sTest));
}


/** Check R-D costs for a CU with PCM mode.
 * \param rpcBestCU pointer to best mode CU data structure
 * \param rpcTempCU pointer to testing mode CU data structure
 * \returns Void
 *
 * \note Current PCM implementation encodes sample values in a lossless way. The distortion of PCM mode CUs are zero. PCM mode is selected if the best mode yields bits greater than that of PCM mode.
 */
Void TEncCu::xCheckIntraPCM(TComDataCU*& rpcBestCU, TComDataCU*& rpcTempCU)
{
	UInt uiDepth = rpcTempCU->getDepth(0);

	rpcTempCU->setSkipFlagSubParts(false, 0, uiDepth);

	rpcTempCU->setIPCMFlag(0, true);
	rpcTempCU->setIPCMFlagSubParts(true, 0, rpcTempCU->getDepth(0));
	rpcTempCU->setPartSizeSubParts(SIZE_2Nx2N, 0, uiDepth);
	rpcTempCU->setPredModeSubParts(MODE_INTRA, 0, uiDepth);
	rpcTempCU->setTrIdxSubParts(0, 0, uiDepth);
	rpcTempCU->setChromaQpAdjSubParts(rpcTempCU->getCUTransquantBypass(0) ? 0 : m_cuChromaQpOffsetIdxPlus1, 0, uiDepth);

	m_pcPredSearch->IPCMSearch(rpcTempCU, m_ppcOrigYuv[uiDepth], m_ppcPredYuvTemp[uiDepth], m_ppcResiYuvTemp[uiDepth], m_ppcRecoYuvTemp[uiDepth]);

	m_pcRDGoOnSbacCoder->load(m_pppcRDSbacCoder[uiDepth][CI_CURR_BEST]);

	m_pcEntropyCoder->resetBits();

	if (rpcTempCU->getSlice()->getPPS()->getTransquantBypassEnableFlag())
	{
		m_pcEntropyCoder->encodeCUTransquantBypassFlag(rpcTempCU, 0, true);
	}

	m_pcEntropyCoder->encodeSkipFlag(rpcTempCU, 0, true);
	m_pcEntropyCoder->encodePredMode(rpcTempCU, 0, true);
	m_pcEntropyCoder->encodePartSize(rpcTempCU, 0, uiDepth, true);
	m_pcEntropyCoder->encodeIPCMInfo(rpcTempCU, 0, true);

	m_pcRDGoOnSbacCoder->store(m_pppcRDSbacCoder[uiDepth][CI_TEMP_BEST]);

	rpcTempCU->getTotalBits() = m_pcEntropyCoder->getNumberOfWrittenBits();
	rpcTempCU->getTotalBins() = ((TEncBinCABAC *)((TEncSbac*)m_pcEntropyCoder->m_pcEntropyCoderIf)->getEncBinIf())->getBinsCoded();
	rpcTempCU->getTotalCost() = m_pcRdCost->calcRdCost(rpcTempCU->getTotalBits(), rpcTempCU->getTotalDistortion());

	xCheckDQP(rpcTempCU);
	DEBUG_STRING_NEW(a)
		DEBUG_STRING_NEW(b)
		xCheckBestMode(rpcBestCU, rpcTempCU, uiDepth DEBUG_STRING_PASS_INTO(a) DEBUG_STRING_PASS_INTO(b));
}

/** check whether current try is the best with identifying the depth of current try
 * \param rpcBestCU
 * \param rpcTempCU
 * \param uiDepth
 */
Void TEncCu::xCheckBestMode(TComDataCU*& rpcBestCU, TComDataCU*& rpcTempCU, UInt uiDepth DEBUG_STRING_FN_DECLARE(sParent) DEBUG_STRING_FN_DECLARE(sTest) DEBUG_STRING_PASS_INTO(Bool bAddSizeInfo))
{
	if (rpcTempCU->getTotalCost() < rpcBestCU->getTotalCost())
	{
		TComYuv* pcYuv;
		// Change Information data
		TComDataCU* pcCU = rpcBestCU;
		rpcBestCU = rpcTempCU;
		rpcTempCU = pcCU;

		// Change Prediction data
		pcYuv = m_ppcPredYuvBest[uiDepth];
		m_ppcPredYuvBest[uiDepth] = m_ppcPredYuvTemp[uiDepth];
		m_ppcPredYuvTemp[uiDepth] = pcYuv;

		// Change Reconstruction data
		pcYuv = m_ppcRecoYuvBest[uiDepth];
		m_ppcRecoYuvBest[uiDepth] = m_ppcRecoYuvTemp[uiDepth];
		m_ppcRecoYuvTemp[uiDepth] = pcYuv;

		pcYuv = NULL;
		pcCU = NULL;

		// store temp best CI for next CU coding
		m_pppcRDSbacCoder[uiDepth][CI_TEMP_BEST]->store(m_pppcRDSbacCoder[uiDepth][CI_NEXT_BEST]);


#if DEBUG_STRING
		DEBUG_STRING_SWAP(sParent, sTest)
			const PredMode predMode = rpcBestCU->getPredictionMode(0);
		if ((DebugOptionList::DebugString_Structure.getInt()&DebugStringGetPredModeMask(predMode)) && bAddSizeInfo)
		{
			std::stringstream ss(stringstream::out);
			ss << "###: " << (predMode == MODE_INTRA ? "Intra   " : "Inter   ") << partSizeToString[rpcBestCU->getPartitionSize(0)] << " CU at " << rpcBestCU->getCUPelX() << ", " << rpcBestCU->getCUPelY() << " width=" << UInt(rpcBestCU->getWidth(0)) << std::endl;
			sParent += ss.str();
		}
#endif
	}
}

Void TEncCu::xCheckDQP(TComDataCU* pcCU)
{
	UInt uiDepth = pcCU->getDepth(0);

	const TComPPS &pps = *(pcCU->getSlice()->getPPS());
	if (pps.getUseDQP() && uiDepth <= pps.getMaxCuDQPDepth())
	{
		if (pcCU->getQtRootCbf(0))
		{
			m_pcEntropyCoder->resetBits();
			m_pcEntropyCoder->encodeQP(pcCU, 0, false);
			pcCU->getTotalBits() += m_pcEntropyCoder->getNumberOfWrittenBits(); // dQP bits
			pcCU->getTotalBins() += ((TEncBinCABAC *)((TEncSbac*)m_pcEntropyCoder->m_pcEntropyCoderIf)->getEncBinIf())->getBinsCoded();
			pcCU->getTotalCost() = m_pcRdCost->calcRdCost(pcCU->getTotalBits(), pcCU->getTotalDistortion());
		}
		else
		{
			pcCU->setQPSubParts(pcCU->getRefQP(0), 0, uiDepth); // set QP to default QP
		}
	}
}

Void TEncCu::xCopyAMVPInfo(AMVPInfo* pSrc, AMVPInfo* pDst)
{
	pDst->iN = pSrc->iN;
	for (Int i = 0; i < pSrc->iN; i++)
	{
		pDst->m_acMvCand[i] = pSrc->m_acMvCand[i];
	}
}
Void TEncCu::xCopyYuv2Pic(TComPic* rpcPic, UInt uiCUAddr, UInt uiAbsPartIdx, UInt uiDepth, UInt uiSrcDepth)
{
	UInt uiAbsPartIdxInRaster = g_auiZscanToRaster[uiAbsPartIdx];
	UInt uiSrcBlkWidth = rpcPic->getNumPartInCtuWidth() >> (uiSrcDepth);
	UInt uiBlkWidth = rpcPic->getNumPartInCtuWidth() >> (uiDepth);
	UInt uiPartIdxX = ((uiAbsPartIdxInRaster % rpcPic->getNumPartInCtuWidth()) % uiSrcBlkWidth) / uiBlkWidth;
	UInt uiPartIdxY = ((uiAbsPartIdxInRaster / rpcPic->getNumPartInCtuWidth()) % uiSrcBlkWidth) / uiBlkWidth;
	UInt uiPartIdx = uiPartIdxY * (uiSrcBlkWidth / uiBlkWidth) + uiPartIdxX;
	m_ppcRecoYuvBest[uiSrcDepth]->copyToPicYuv(rpcPic->getPicYuvRec(), uiCUAddr, uiAbsPartIdx, uiDepth - uiSrcDepth, uiPartIdx);

	m_ppcPredYuvBest[uiSrcDepth]->copyToPicYuv(rpcPic->getPicYuvPred(), uiCUAddr, uiAbsPartIdx, uiDepth - uiSrcDepth, uiPartIdx);
}

Void TEncCu::xCopyYuv2Tmp(UInt uiPartUnitIdx, UInt uiNextDepth)
{
	UInt uiCurrDepth = uiNextDepth - 1;
	m_ppcRecoYuvBest[uiNextDepth]->copyToPartYuv(m_ppcRecoYuvTemp[uiCurrDepth], uiPartUnitIdx);
	m_ppcPredYuvBest[uiNextDepth]->copyToPartYuv(m_ppcPredYuvBest[uiCurrDepth], uiPartUnitIdx);
}

/** Function for filling the PCM buffer of a CU using its original sample array
 * \param pCU pointer to current CU
 * \param pOrgYuv pointer to original sample array
 */
Void TEncCu::xFillPCMBuffer(TComDataCU* pCU, TComYuv* pOrgYuv)
{
	const ChromaFormat format = pCU->getPic()->getChromaFormat();
	const UInt numberValidComponents = getNumberValidComponents(format);
	for (UInt componentIndex = 0; componentIndex < numberValidComponents; componentIndex++)
	{
		const ComponentID component = ComponentID(componentIndex);

		const UInt width = pCU->getWidth(0) >> getComponentScaleX(component, format);
		const UInt height = pCU->getHeight(0) >> getComponentScaleY(component, format);

		Pel *source = pOrgYuv->getAddr(component, 0, width);
		Pel *destination = pCU->getPCMSample(component);

		const UInt sourceStride = pOrgYuv->getStride(component);

		for (Int line = 0; line < height; line++)
		{
			for (Int column = 0; column < width; column++)
			{
				destination[column] = source[column];
			}

			source += sourceStride;
			destination += width;
		}
	}
}

#if ADAPTIVE_QP_SELECTION
/** Collect ARL statistics from one block
  */
Int TEncCu::xTuCollectARLStats(TCoeff* rpcCoeff, TCoeff* rpcArlCoeff, Int NumCoeffInCU, Double* cSum, UInt* numSamples)
{
	for (Int n = 0; n < NumCoeffInCU; n++)
	{
		TCoeff u = abs(rpcCoeff[n]);
		TCoeff absc = rpcArlCoeff[n];

		if (u != 0)
		{
			if (u < LEVEL_RANGE)
			{
				cSum[u] += (Double)absc;
				numSamples[u]++;
			}
			else
			{
				cSum[LEVEL_RANGE] += (Double)absc - (Double)(u << ARL_C_PRECISION);
				numSamples[LEVEL_RANGE]++;
			}
		}
	}

	return 0;
}

//! Collect ARL statistics from one CTU
Void TEncCu::xCtuCollectARLStats(TComDataCU* pCtu)
{
	Double cSum[LEVEL_RANGE + 1];     //: the sum of DCT coefficients corresponding to data type and quantization output
	UInt numSamples[LEVEL_RANGE + 1]; //: the number of coefficients corresponding to data type and quantization output

	TCoeff* pCoeffY = pCtu->getCoeff(COMPONENT_Y);
	TCoeff* pArlCoeffY = pCtu->getArlCoeff(COMPONENT_Y);
	const TComSPS &sps = *(pCtu->getSlice()->getSPS());

	const UInt uiMinCUWidth = sps.getMaxCUWidth() >> sps.getMaxTotalCUDepth(); // NOTE: ed - this is not the minimum CU width. It is the square-root of the number of coefficients per part.
	const UInt uiMinNumCoeffInCU = 1 << uiMinCUWidth;                          // NOTE: ed - what is this?

	memset(cSum, 0, sizeof(Double)*(LEVEL_RANGE + 1));
	memset(numSamples, 0, sizeof(UInt)*(LEVEL_RANGE + 1));

	// Collect stats to cSum[][] and numSamples[][]
	for (Int i = 0; i < pCtu->getTotalNumPart(); i++)
	{
		UInt uiTrIdx = pCtu->getTransformIdx(i);

		if (pCtu->isInter(i) && pCtu->getCbf(i, COMPONENT_Y, uiTrIdx))
		{
			xTuCollectARLStats(pCoeffY, pArlCoeffY, uiMinNumCoeffInCU, cSum, numSamples);
		}//Note that only InterY is processed. QP rounding is based on InterY data only.

		pCoeffY += uiMinNumCoeffInCU;
		pArlCoeffY += uiMinNumCoeffInCU;
	}

	for (Int u = 1; u < LEVEL_RANGE; u++)
	{
		m_pcTrQuant->getSliceSumC()[u] += cSum[u];
		m_pcTrQuant->getSliceNSamples()[u] += numSamples[u];
	}
	m_pcTrQuant->getSliceSumC()[LEVEL_RANGE] += cSum[LEVEL_RANGE];
	m_pcTrQuant->getSliceNSamples()[LEVEL_RANGE] += numSamples[LEVEL_RANGE];
}
#endif
//! \}