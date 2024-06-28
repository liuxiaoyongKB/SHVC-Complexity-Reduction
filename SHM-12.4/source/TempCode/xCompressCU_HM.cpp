#if AMP_ENC_SPEEDUP
Void TEncCu::xCompressCU(TComDataCU*& rpcBestCU, TComDataCU*& rpcTempCU, UInt uiDepth DEBUG_STRING_FN_DECLARE(sDebug_), PartSize eParentPartSize)
#else
Void TEncCu::xCompressCU(TComDataCU*& rpcBestCU, TComDataCU*& rpcTempCU, UInt uiDepth)
#endif
{
	TComPic* pcPic = rpcBestCU->getPic();
	DEBUG_STRING_NEW(sDebug)
		const TComPPS &pps = *(rpcTempCU->getSlice()->getPPS());
	const TComSPS &sps = *(rpcTempCU->getSlice()->getSPS());

	// get Original YUV data from picture
	m_ppcOrigYuv[uiDepth]->copyFromPicYuv(pcPic->getPicYuvOrg(), rpcBestCU->getCtuRsAddr(), rpcBestCU->getZorderIdxInCtu());

	// variable for Early CU determination
	Bool    bSubBranch = true;

	// variable for Cbf fast mode PU decision
	Bool    doNotBlockPu = true;
	Bool    earlyDetectionSkipMode = false;

	Bool bBoundary = false;
	UInt uiLPelX = rpcBestCU->getCUPelX();
	UInt uiRPelX = uiLPelX + rpcBestCU->getWidth(0) - 1;
	UInt uiTPelY = rpcBestCU->getCUPelY();
	UInt uiBPelY = uiTPelY + rpcBestCU->getHeight(0) - 1;

	Int iBaseQP = xComputeQP(rpcBestCU, uiDepth);
	Int iMinQP;
	Int iMaxQP;
	Bool isAddLowestQP = false;

	const UInt numberValidComponents = rpcBestCU->getPic()->getNumberValidComponents();

	if (uiDepth <= pps.getMaxCuDQPDepth())
	{
		Int idQP = m_pcEncCfg->getMaxDeltaQP();
		iMinQP = Clip3(-sps.getQpBDOffset(CHANNEL_TYPE_LUMA), MAX_QP, iBaseQP - idQP);
		iMaxQP = Clip3(-sps.getQpBDOffset(CHANNEL_TYPE_LUMA), MAX_QP, iBaseQP + idQP);
	}
	else
	{
		iMinQP = rpcTempCU->getQP(0);
		iMaxQP = rpcTempCU->getQP(0);
	}

	if (m_pcEncCfg->getUseRateCtrl())
	{
		iMinQP = m_pcRateCtrl->getRCQP();
		iMaxQP = m_pcRateCtrl->getRCQP();
	}

	// transquant-bypass (TQB) processing loop variable initialisation ---

	const Int lowestQP = iMinQP; // For TQB, use this QP which is the lowest non TQB QP tested (rather than QP'=0) - that way delta QPs are smaller, and TQB can be tested at all CU levels.

	if ((pps.getTransquantBypassEnableFlag()))
	{
		isAddLowestQP = true; // mark that the first iteration is to cost TQB mode.
		iMinQP = iMinQP - 1;  // increase loop variable range by 1, to allow testing of TQB mode along with other QPs
		if (m_pcEncCfg->getCUTransquantBypassFlagForceValue())
		{
			iMaxQP = iMinQP;
		}
	}

	TComSlice * pcSlice = rpcTempCU->getPic()->getSlice(rpcTempCU->getPic()->getCurrSliceIdx());
	// We need to split, so don't try these modes.
	if ((uiRPelX < sps.getPicWidthInLumaSamples()) &&
		(uiBPelY < sps.getPicHeightInLumaSamples()))
	{
		for (Int iQP = iMinQP; iQP <= iMaxQP; iQP++)
		{
			const Bool bIsLosslessMode = isAddLowestQP && (iQP == iMinQP);

			if (bIsLosslessMode)
			{
				iQP = lowestQP;
			}

			m_cuChromaQpOffsetIdxPlus1 = 0;
			if (pcSlice->getUseChromaQpAdj())
			{
				/* Pre-estimation of chroma QP based on input block activity may be performed
				 * here, using for example m_ppcOrigYuv[uiDepth] */
				 /* To exercise the current code, the index used for adjustment is based on
				  * block position
				  */
				Int lgMinCuSize = sps.getLog2MinCodingBlockSize() +
					std::max<Int>(0, sps.getLog2DiffMaxMinCodingBlockSize() - Int(pps.getPpsRangeExtension().getDiffCuChromaQpOffsetDepth()));
				m_cuChromaQpOffsetIdxPlus1 = ((uiLPelX >> lgMinCuSize) + (uiTPelY >> lgMinCuSize)) % (pps.getPpsRangeExtension().getChromaQpOffsetListLen() + 1);
			}

			rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);

			// do inter modes, SKIP and 2Nx2N
			if (rpcBestCU->getSlice()->getSliceType() != I_SLICE)
			{
				// 2Nx2N
				if (m_pcEncCfg->getUseEarlySkipDetection())
				{
					xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_2Nx2N DEBUG_STRING_PASS_INTO(sDebug));
					rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);//by Competition for inter_2Nx2N
				}
				// SKIP
				xCheckRDCostMerge2Nx2N(rpcBestCU, rpcTempCU DEBUG_STRING_PASS_INTO(sDebug), &earlyDetectionSkipMode);//by Merge for inter_2Nx2N
				rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);

				if (!m_pcEncCfg->getUseEarlySkipDetection())
				{
					// 2Nx2N, NxN
					xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_2Nx2N DEBUG_STRING_PASS_INTO(sDebug));
					rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
					if (m_pcEncCfg->getUseCbfFastMode())
					{
						doNotBlockPu = rpcBestCU->getQtRootCbf(0) != 0;
					}
				}
			}

			if (bIsLosslessMode) // Restore loop variable if lossless mode was searched.
			{
				iQP = iMinQP;
			}
		}

		if (!earlyDetectionSkipMode)
		{
			for (Int iQP = iMinQP; iQP <= iMaxQP; iQP++)
			{
				const Bool bIsLosslessMode = isAddLowestQP && (iQP == iMinQP); // If lossless, then iQP is irrelevant for subsequent modules.

				if (bIsLosslessMode)
				{
					iQP = lowestQP;
				}

				rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);

				// do inter modes, NxN, 2NxN, and Nx2N
				if (rpcBestCU->getSlice()->getSliceType() != I_SLICE)
				{
					// 2Nx2N, NxN

					if (!((rpcBestCU->getWidth(0) == 8) && (rpcBestCU->getHeight(0) == 8)))
					{
						if (uiDepth == sps.getLog2DiffMaxMinCodingBlockSize() && doNotBlockPu)
						{
							xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_NxN DEBUG_STRING_PASS_INTO(sDebug));
							rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
						}
					}

					if (doNotBlockPu)
					{
						xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_Nx2N DEBUG_STRING_PASS_INTO(sDebug));
						rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
						if (m_pcEncCfg->getUseCbfFastMode() && rpcBestCU->getPartitionSize(0) == SIZE_Nx2N)
						{
							doNotBlockPu = rpcBestCU->getQtRootCbf(0) != 0;
						}
					}
					if (doNotBlockPu)
					{
						xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_2NxN DEBUG_STRING_PASS_INTO(sDebug));
						rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
						if (m_pcEncCfg->getUseCbfFastMode() && rpcBestCU->getPartitionSize(0) == SIZE_2NxN)
						{
							doNotBlockPu = rpcBestCU->getQtRootCbf(0) != 0;
						}
					}

					//! Try AMP (SIZE_2NxnU, SIZE_2NxnD, SIZE_nLx2N, SIZE_nRx2N)
					if (sps.getUseAMP() && uiDepth < sps.getLog2DiffMaxMinCodingBlockSize())
					{
#if AMP_ENC_SPEEDUP
						Bool bTestAMP_Hor = false, bTestAMP_Ver = false;

#if AMP_MRG
						Bool bTestMergeAMP_Hor = false, bTestMergeAMP_Ver = false;

						deriveTestModeAMP(rpcBestCU, eParentPartSize, bTestAMP_Hor, bTestAMP_Ver, bTestMergeAMP_Hor, bTestMergeAMP_Ver);
#else
						deriveTestModeAMP(rpcBestCU, eParentPartSize, bTestAMP_Hor, bTestAMP_Ver);
#endif

						//! Do horizontal AMP
						if (bTestAMP_Hor)
						{
							if (doNotBlockPu)
							{
								xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_2NxnU DEBUG_STRING_PASS_INTO(sDebug));
								rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
								if (m_pcEncCfg->getUseCbfFastMode() && rpcBestCU->getPartitionSize(0) == SIZE_2NxnU)
								{
									doNotBlockPu = rpcBestCU->getQtRootCbf(0) != 0;
								}
							}
							if (doNotBlockPu)
							{
								xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_2NxnD DEBUG_STRING_PASS_INTO(sDebug));
								rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
								if (m_pcEncCfg->getUseCbfFastMode() && rpcBestCU->getPartitionSize(0) == SIZE_2NxnD)
								{
									doNotBlockPu = rpcBestCU->getQtRootCbf(0) != 0;
								}
							}
						}
#if AMP_MRG
						else if (bTestMergeAMP_Hor)
						{
							if (doNotBlockPu)
							{
								xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_2NxnU DEBUG_STRING_PASS_INTO(sDebug), true);
								rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
								if (m_pcEncCfg->getUseCbfFastMode() && rpcBestCU->getPartitionSize(0) == SIZE_2NxnU)
								{
									doNotBlockPu = rpcBestCU->getQtRootCbf(0) != 0;
								}
							}
							if (doNotBlockPu)
							{
								xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_2NxnD DEBUG_STRING_PASS_INTO(sDebug), true);
								rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
								if (m_pcEncCfg->getUseCbfFastMode() && rpcBestCU->getPartitionSize(0) == SIZE_2NxnD)
								{
									doNotBlockPu = rpcBestCU->getQtRootCbf(0) != 0;
								}
							}
						}
#endif

						//! Do horizontal AMP
						if (bTestAMP_Ver)
						{
							if (doNotBlockPu)
							{
								xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_nLx2N DEBUG_STRING_PASS_INTO(sDebug));
								rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
								if (m_pcEncCfg->getUseCbfFastMode() && rpcBestCU->getPartitionSize(0) == SIZE_nLx2N)
								{
									doNotBlockPu = rpcBestCU->getQtRootCbf(0) != 0;
								}
							}
							if (doNotBlockPu)
							{
								xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_nRx2N DEBUG_STRING_PASS_INTO(sDebug));
								rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
							}
						}
#if AMP_MRG
						else if (bTestMergeAMP_Ver)
						{
							if (doNotBlockPu)
							{
								xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_nLx2N DEBUG_STRING_PASS_INTO(sDebug), true);
								rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
								if (m_pcEncCfg->getUseCbfFastMode() && rpcBestCU->getPartitionSize(0) == SIZE_nLx2N)
								{
									doNotBlockPu = rpcBestCU->getQtRootCbf(0) != 0;
								}
							}
							if (doNotBlockPu)
							{
								xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_nRx2N DEBUG_STRING_PASS_INTO(sDebug), true);
								rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
							}
						}
#endif

#else
						xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_2NxnU);
						rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
						xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_2NxnD);
						rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
						xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_nLx2N);
						rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);

						xCheckRDCostInter(rpcBestCU, rpcTempCU, SIZE_nRx2N);
						rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);

#endif
					}
				}

				// do normal intra modes
				// speedup for inter frames
				Double intraCost = 0.0;

				if ((rpcBestCU->getSlice()->getSliceType() == I_SLICE) ||
					((!m_pcEncCfg->getDisableIntraPUsInInterSlices()) && (
					(rpcBestCU->getCbf(0, COMPONENT_Y) != 0) ||
						((rpcBestCU->getCbf(0, COMPONENT_Cb) != 0) && (numberValidComponents > COMPONENT_Cb)) ||
						((rpcBestCU->getCbf(0, COMPONENT_Cr) != 0) && (numberValidComponents > COMPONENT_Cr))  // avoid very complex intra if it is unlikely
						)))
				{
					xCheckRDCostIntra(rpcBestCU, rpcTempCU, intraCost, SIZE_2Nx2N DEBUG_STRING_PASS_INTO(sDebug));
					rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
					if (uiDepth == sps.getLog2DiffMaxMinCodingBlockSize())
					{
						if (rpcTempCU->getWidth(0) > (1 << sps.getQuadtreeTULog2MinSize()))
						{
							Double tmpIntraCost;
							xCheckRDCostIntra(rpcBestCU, rpcTempCU, tmpIntraCost, SIZE_NxN DEBUG_STRING_PASS_INTO(sDebug));
							intraCost = std::min(intraCost, tmpIntraCost);
							rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
						}
					}
				}

				// test PCM
				if (sps.getUsePCM()
					&& rpcTempCU->getWidth(0) <= (1 << sps.getPCMLog2MaxSize())
					&& rpcTempCU->getWidth(0) >= (1 << sps.getPCMLog2MinSize()))
				{
					UInt uiRawBits = getTotalBits(rpcBestCU->getWidth(0), rpcBestCU->getHeight(0), rpcBestCU->getPic()->getChromaFormat(), sps.getBitDepths().recon);
					UInt uiBestBits = rpcBestCU->getTotalBits();
					if ((uiBestBits > uiRawBits) || (rpcBestCU->getTotalCost() > m_pcRdCost->calcRdCost(uiRawBits, 0)))
					{
						xCheckIntraPCM(rpcBestCU, rpcTempCU);
						rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);
					}
				}

				if (bIsLosslessMode) // Restore loop variable if lossless mode was searched.
				{
					iQP = iMinQP;
				}
			}
		}

		m_pcRDGoOnSbacCoder->load(m_pppcRDSbacCoder[uiDepth][CI_NEXT_BEST]);
		m_pcEntropyCoder->resetBits();
		m_pcEntropyCoder->encodeSplitFlag(rpcBestCU, 0, uiDepth, true);
		rpcBestCU->getTotalBits() += m_pcEntropyCoder->getNumberOfWrittenBits(); // split bits
		rpcBestCU->getTotalBins() += ((TEncBinCABAC *)((TEncSbac*)m_pcEntropyCoder->m_pcEntropyCoderIf)->getEncBinIf())->getBinsCoded();
		rpcBestCU->getTotalCost() = m_pcRdCost->calcRdCost(rpcBestCU->getTotalBits(), rpcBestCU->getTotalDistortion());
		m_pcRDGoOnSbacCoder->store(m_pppcRDSbacCoder[uiDepth][CI_NEXT_BEST]);

		// Early CU determination
		if (m_pcEncCfg->getUseEarlyCU() && rpcBestCU->isSkipped(0))
		{
			bSubBranch = false;
		}
		else
		{
			bSubBranch = true;
		}
	}
	else
	{
		bBoundary = true;
	}

	// copy orginal YUV samples to PCM buffer
	if (rpcBestCU->isLosslessCoded(0) && (rpcBestCU->getIPCMFlag(0) == false))
	{
		xFillPCMBuffer(rpcBestCU, m_ppcOrigYuv[uiDepth]);
	}

	if (uiDepth == pps.getMaxCuDQPDepth())
	{
		Int idQP = m_pcEncCfg->getMaxDeltaQP();
		iMinQP = Clip3(-sps.getQpBDOffset(CHANNEL_TYPE_LUMA), MAX_QP, iBaseQP - idQP);
		iMaxQP = Clip3(-sps.getQpBDOffset(CHANNEL_TYPE_LUMA), MAX_QP, iBaseQP + idQP);
	}
	else if (uiDepth < pps.getMaxCuDQPDepth())
	{
		iMinQP = iBaseQP;
		iMaxQP = iBaseQP;
	}
	else
	{
		const Int iStartQP = rpcTempCU->getQP(0);
		iMinQP = iStartQP;
		iMaxQP = iStartQP;
	}

	if (m_pcEncCfg->getUseRateCtrl())
	{
		iMinQP = m_pcRateCtrl->getRCQP();
		iMaxQP = m_pcRateCtrl->getRCQP();
	}

	if (m_pcEncCfg->getCUTransquantBypassFlagForceValue())
	{
		iMaxQP = iMinQP; // If all TUs are forced into using transquant bypass, do not loop here.
	}

	for (Int iQP = iMinQP; iQP <= iMaxQP; iQP++)
	{
		const Bool bIsLosslessMode = false; // False at this level. Next level down may set it to true.

		rpcTempCU->initEstData(uiDepth, iQP, bIsLosslessMode);

		// further split
		if (bSubBranch && uiDepth < sps.getLog2DiffMaxMinCodingBlockSize())
		{
			UChar       uhNextDepth = uiDepth + 1;
			TComDataCU* pcSubBestPartCU = m_ppcBestCU[uhNextDepth];
			TComDataCU* pcSubTempPartCU = m_ppcTempCU[uhNextDepth];
			DEBUG_STRING_NEW(sTempDebug)

				for (UInt uiPartUnitIdx = 0; uiPartUnitIdx < 4; uiPartUnitIdx++)
				{
					pcSubBestPartCU->initSubCU(rpcTempCU, uiPartUnitIdx, uhNextDepth, iQP);           // clear sub partition datas or init.
					pcSubTempPartCU->initSubCU(rpcTempCU, uiPartUnitIdx, uhNextDepth, iQP);           // clear sub partition datas or init.

					if ((pcSubBestPartCU->getCUPelX() < sps.getPicWidthInLumaSamples()) && (pcSubBestPartCU->getCUPelY() < sps.getPicHeightInLumaSamples()))
					{
						if (0 == uiPartUnitIdx) //initialize RD with previous depth buffer
						{
							m_pppcRDSbacCoder[uhNextDepth][CI_CURR_BEST]->load(m_pppcRDSbacCoder[uiDepth][CI_CURR_BEST]);
						}
						else
						{
							m_pppcRDSbacCoder[uhNextDepth][CI_CURR_BEST]->load(m_pppcRDSbacCoder[uhNextDepth][CI_NEXT_BEST]);
						}

#if AMP_ENC_SPEEDUP
						DEBUG_STRING_NEW(sChild)
							if (!rpcBestCU->isInter(0))
							{
								xCompressCU(pcSubBestPartCU, pcSubTempPartCU, uhNextDepth DEBUG_STRING_PASS_INTO(sChild), NUMBER_OF_PART_SIZES);
							}
							else
							{

								xCompressCU(pcSubBestPartCU, pcSubTempPartCU, uhNextDepth DEBUG_STRING_PASS_INTO(sChild), rpcBestCU->getPartitionSize(0));
							}
						DEBUG_STRING_APPEND(sTempDebug, sChild)
#else
						xCompressCU(pcSubBestPartCU, pcSubTempPartCU, uhNextDepth);
#endif

						rpcTempCU->copyPartFrom(pcSubBestPartCU, uiPartUnitIdx, uhNextDepth);         // Keep best part data to current temporary data.
						xCopyYuv2Tmp(pcSubBestPartCU->getTotalNumPart()*uiPartUnitIdx, uhNextDepth);
					}
					else
					{
						pcSubBestPartCU->copyToPic(uhNextDepth);
						rpcTempCU->copyPartFrom(pcSubBestPartCU, uiPartUnitIdx, uhNextDepth);
					}
				}

			m_pcRDGoOnSbacCoder->load(m_pppcRDSbacCoder[uhNextDepth][CI_NEXT_BEST]);
			if (!bBoundary)
			{
				m_pcEntropyCoder->resetBits();
				m_pcEntropyCoder->encodeSplitFlag(rpcTempCU, 0, uiDepth, true);

				rpcTempCU->getTotalBits() += m_pcEntropyCoder->getNumberOfWrittenBits(); // split bits
				rpcTempCU->getTotalBins() += ((TEncBinCABAC *)((TEncSbac*)m_pcEntropyCoder->m_pcEntropyCoderIf)->getEncBinIf())->getBinsCoded();
			}
			rpcTempCU->getTotalCost() = m_pcRdCost->calcRdCost(rpcTempCU->getTotalBits(), rpcTempCU->getTotalDistortion());

			if (uiDepth == pps.getMaxCuDQPDepth() && pps.getUseDQP())
			{
				Bool hasResidual = false;
				for (UInt uiBlkIdx = 0; uiBlkIdx < rpcTempCU->getTotalNumPart(); uiBlkIdx++)
				{
					if ((rpcTempCU->getCbf(uiBlkIdx, COMPONENT_Y)
						|| (rpcTempCU->getCbf(uiBlkIdx, COMPONENT_Cb) && (numberValidComponents > COMPONENT_Cb))
						|| (rpcTempCU->getCbf(uiBlkIdx, COMPONENT_Cr) && (numberValidComponents > COMPONENT_Cr))))
					{
						hasResidual = true;
						break;
					}
				}

				UInt uiTargetPartIdx = 0;
				if (hasResidual)
				{
					m_pcEntropyCoder->resetBits();
					m_pcEntropyCoder->encodeQP(rpcTempCU, uiTargetPartIdx, false);
					rpcTempCU->getTotalBits() += m_pcEntropyCoder->getNumberOfWrittenBits(); // dQP bits
					rpcTempCU->getTotalBins() += ((TEncBinCABAC *)((TEncSbac*)m_pcEntropyCoder->m_pcEntropyCoderIf)->getEncBinIf())->getBinsCoded();
					rpcTempCU->getTotalCost() = m_pcRdCost->calcRdCost(rpcTempCU->getTotalBits(), rpcTempCU->getTotalDistortion());

					Bool foundNonZeroCbf = false;
					rpcTempCU->setQPSubCUs(rpcTempCU->getRefQP(uiTargetPartIdx), 0, uiDepth, foundNonZeroCbf);
					assert(foundNonZeroCbf);
				}
				else
				{
					rpcTempCU->setQPSubParts(rpcTempCU->getRefQP(uiTargetPartIdx), 0, uiDepth); // set QP to default QP
				}
			}

			m_pcRDGoOnSbacCoder->store(m_pppcRDSbacCoder[uiDepth][CI_TEMP_BEST]);

			// If the configuration being tested exceeds the maximum number of bytes for a slice / slice-segment, then
			// a proper RD evaluation cannot be performed. Therefore, termination of the
			// slice/slice-segment must be made prior to this CTU.
			// This can be achieved by forcing the decision to be that of the rpcTempCU.
			// The exception is each slice / slice-segment must have at least one CTU.
			const Bool isEndOfSlice = pcSlice->getSliceMode() == FIXED_NUMBER_OF_BYTES
				&& ((pcSlice->getSliceBits() + rpcBestCU->getTotalBits()) > pcSlice->getSliceArgument() << 3)
				&& rpcBestCU->getCtuRsAddr() != pcPic->getPicSym()->getCtuTsToRsAddrMap(pcSlice->getSliceCurStartCtuTsAddr())
				&& rpcBestCU->getCtuRsAddr() != pcPic->getPicSym()->getCtuTsToRsAddrMap(pcSlice->getSliceSegmentCurStartCtuTsAddr());
			const Bool isEndOfSliceSegment = pcSlice->getSliceSegmentMode() == FIXED_NUMBER_OF_BYTES
				&& ((pcSlice->getSliceSegmentBits() + rpcBestCU->getTotalBits()) > pcSlice->getSliceSegmentArgument() << 3)
				&& rpcBestCU->getCtuRsAddr() != pcPic->getPicSym()->getCtuTsToRsAddrMap(pcSlice->getSliceSegmentCurStartCtuTsAddr());
			// Do not need to check slice condition for slice-segment since a slice-segment is a subset of a slice.
			if (isEndOfSlice || isEndOfSliceSegment)
			{
				rpcBestCU->getTotalCost() = MAX_DOUBLE;
			}

			xCheckBestMode(rpcBestCU, rpcTempCU, uiDepth DEBUG_STRING_PASS_INTO(sDebug) DEBUG_STRING_PASS_INTO(sTempDebug) DEBUG_STRING_PASS_INTO(false)); // RD compare current larger prediction
																							 // with sub partitioned prediction.
		}
	}

	DEBUG_STRING_APPEND(sDebug_, sDebug);

	rpcBestCU->copyToPic(uiDepth);                                                     // Copy Best data to Picture for next partition prediction.

	xCopyYuv2Pic(rpcBestCU->getPic(), rpcBestCU->getCtuRsAddr(), rpcBestCU->getZorderIdxInCtu(), uiDepth, uiDepth);   // Copy Yuv data to picture Yuv
	if (bBoundary)
	{
		return;
	}

	// Assert if Best prediction mode is NONE
	// Selected mode's RD-cost must be not MAX_DOUBLE.
	assert(rpcBestCU->getPartitionSize(0) != NUMBER_OF_PART_SIZES);
	assert(rpcBestCU->getPredictionMode(0) != NUMBER_OF_PREDICTION_MODES);
	assert(rpcBestCU->getTotalCost() != MAX_DOUBLE);
}