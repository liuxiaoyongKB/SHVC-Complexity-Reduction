/** \param pcPic   picture class
 */
Void TEncSlice::compressSlice(TComPic* pcPic, const Bool bCompressEntireSlice)
{
	// if bCompressEntireSlice is true, then the entire slice (not slice segment) is compressed,
	//   effectively disabling the slice-segment-mode.

	UInt   startCtuTsAddr;
	UInt   boundingCtuTsAddr;
	TComSlice* const pcSlice = pcPic->getSlice(getSliceIdx());
	pcSlice->setSliceSegmentBits(0);
	xDetermineStartAndBoundingCtuTsAddr(startCtuTsAddr, boundingCtuTsAddr, pcPic);
	if (bCompressEntireSlice)
	{
		boundingCtuTsAddr = pcSlice->getSliceCurEndCtuTsAddr();
		pcSlice->setSliceSegmentCurEndCtuTsAddr(boundingCtuTsAddr);
	}

	// initialize cost values - these are used by precompressSlice (they should be parameters).
	m_uiPicTotalBits = 0;
	m_dPicRdCost = 0; // NOTE: This is a write-only variable!
	m_uiPicDist = 0;

	m_pcEntropyCoder->setEntropyCoder(m_pppcRDSbacCoder[0][CI_CURR_BEST]);
	m_pcEntropyCoder->resetEntropy(pcSlice);

	TEncBinCABAC* pRDSbacCoder = (TEncBinCABAC *)m_pppcRDSbacCoder[0][CI_CURR_BEST]->getEncBinIf();
	pRDSbacCoder->setBinCountingEnableFlag(false);
	pRDSbacCoder->setBinsCoded(0);

	TComBitCounter  tempBitCounter;
	const UInt      frameWidthInCtus = pcPic->getPicSym()->getFrameWidthInCtus();

	//------------------------------------------------------------------------------
	//  Weighted Prediction parameters estimation.
	//------------------------------------------------------------------------------
	// calculate AC/DC values for current picture
	if (pcSlice->getPPS()->getUseWP() || pcSlice->getPPS()->getWPBiPred())
	{
		xCalcACDCParamSlice(pcSlice);
	}

	const Bool bWp_explicit = (pcSlice->getSliceType() == P_SLICE && pcSlice->getPPS()->getUseWP()) || (pcSlice->getSliceType() == B_SLICE && pcSlice->getPPS()->getWPBiPred());

	if (bWp_explicit)
	{
		//------------------------------------------------------------------------------
		//  Weighted Prediction implemented at Slice level. SliceMode=2 is not supported yet.
		//------------------------------------------------------------------------------
		if (pcSlice->getSliceMode() == FIXED_NUMBER_OF_BYTES || pcSlice->getSliceSegmentMode() == FIXED_NUMBER_OF_BYTES)
		{
			printf("Weighted Prediction is not supported with slice mode determined by max number of bins.\n"); exit(0);
		}

		xEstimateWPParamSlice(pcSlice);
		pcSlice->initWpScaling(pcSlice->getSPS());

		// check WP on/off
		xCheckWPEnable(pcSlice);
	}

#if ADAPTIVE_QP_SELECTION
	if (m_pcCfg->getUseAdaptQpSelect() && !(pcSlice->getDependentSliceSegmentFlag()))
	{
		// TODO: this won't work with dependent slices: they do not have their own QP. Check fix to mask clause execution with && !(pcSlice->getDependentSliceSegmentFlag())
		m_pcTrQuant->clearSliceARLCnt(); // TODO: this looks wrong for multiple slices - the results of all but the last slice will be cleared before they are used (all slices compressed, and then all slices encoded)
		if (pcSlice->getSliceType() != I_SLICE)
		{
			Int qpBase = pcSlice->getSliceQpBase();
			pcSlice->setSliceQp(qpBase + m_pcTrQuant->getQpDelta(qpBase));
		}
	}
#endif



	// Adjust initial state if this is the start of a dependent slice.
	{
		const UInt      ctuRsAddr = pcPic->getPicSym()->getCtuTsToRsAddrMap(startCtuTsAddr);
		const UInt      currentTileIdx = pcPic->getPicSym()->getTileIdxMap(ctuRsAddr);
		const TComTile *pCurrentTile = pcPic->getPicSym()->getTComTile(currentTileIdx);
		const UInt      firstCtuRsAddrOfTile = pCurrentTile->getFirstCtuRsAddr();
		if (pcSlice->getDependentSliceSegmentFlag() && ctuRsAddr != firstCtuRsAddrOfTile)
		{
			// This will only occur if dependent slice-segments (m_entropyCodingSyncContextState=true) are being used.
			if (pCurrentTile->getTileWidthInCtus() >= 2 || !m_pcCfg->getWaveFrontsynchro())
			{
				m_pppcRDSbacCoder[0][CI_CURR_BEST]->loadContexts(&m_lastSliceSegmentEndContextState);
			}
		}
	}

	// for every CTU in the slice segment (may terminate sooner if there is a byte limit on the slice-segment)

	for (UInt ctuTsAddr = startCtuTsAddr; ctuTsAddr < boundingCtuTsAddr; ++ctuTsAddr)
	{
		const UInt ctuRsAddr = pcPic->getPicSym()->getCtuTsToRsAddrMap(ctuTsAddr);
		// initialize CTU encoder
		TComDataCU* pCtu = pcPic->getCtu(ctuRsAddr);
		pCtu->initCtu(pcPic, ctuRsAddr);

		// update CABAC state
		const UInt firstCtuRsAddrOfTile = pcPic->getPicSym()->getTComTile(pcPic->getPicSym()->getTileIdxMap(ctuRsAddr))->getFirstCtuRsAddr();
		const UInt tileXPosInCtus = firstCtuRsAddrOfTile % frameWidthInCtus;
		const UInt ctuXPosInCtus = ctuRsAddr % frameWidthInCtus;

		if (ctuRsAddr == firstCtuRsAddrOfTile)
		{
			m_pppcRDSbacCoder[0][CI_CURR_BEST]->resetEntropy(pcSlice);
		}
		else if (ctuXPosInCtus == tileXPosInCtus && m_pcCfg->getWaveFrontsynchro())
		{
			// reset and then update contexts to the state at the end of the top-right CTU (if within current slice and tile).
			m_pppcRDSbacCoder[0][CI_CURR_BEST]->resetEntropy(pcSlice);
			// Sync if the Top-Right is available.
			TComDataCU *pCtuUp = pCtu->getCtuAbove();
			if (pCtuUp && ((ctuRsAddr%frameWidthInCtus + 1) < frameWidthInCtus))
			{
				TComDataCU *pCtuTR = pcPic->getCtu(ctuRsAddr - frameWidthInCtus + 1);
				if (pCtu->CUIsFromSameSliceAndTile(pCtuTR))
				{
					// Top-Right is available, we use it.
					m_pppcRDSbacCoder[0][CI_CURR_BEST]->loadContexts(&m_entropyCodingSyncContextState);
				}
			}
		}

		// set go-on entropy coder (used for all trial encodings - the cu encoder and encoder search also have a copy of the same pointer)
		m_pcEntropyCoder->setEntropyCoder(m_pcRDGoOnSbacCoder);
		m_pcEntropyCoder->setBitstream(&tempBitCounter);
		tempBitCounter.resetBits();
		m_pcRDGoOnSbacCoder->load(m_pppcRDSbacCoder[0][CI_CURR_BEST]); // this copy is not strictly necessary here, but indicates that the GoOnSbacCoder
																		 // is reset to a known state before every decision process.

		((TEncBinCABAC*)m_pcRDGoOnSbacCoder->getEncBinIf())->setBinCountingEnableFlag(true);

		Double oldLambda = m_pcRdCost->getLambda();
		if (m_pcCfg->getUseRateCtrl())
		{
			Int estQP = pcSlice->getSliceQp();
			Double estLambda = -1.0;
			Double bpp = -1.0;

			if ((pcPic->getSlice(0)->getSliceType() == I_SLICE && m_pcCfg->getForceIntraQP()) || !m_pcCfg->getLCULevelRC())
			{
				estQP = pcSlice->getSliceQp();
			}
			else
			{
				bpp = m_pcRateCtrl->getRCPic()->getLCUTargetBpp(pcSlice->getSliceType());
				if (pcPic->getSlice(0)->getSliceType() == I_SLICE)
				{
					estLambda = m_pcRateCtrl->getRCPic()->getLCUEstLambdaAndQP(bpp, pcSlice->getSliceQp(), &estQP);
				}
				else
				{
					estLambda = m_pcRateCtrl->getRCPic()->getLCUEstLambda(bpp);
					estQP = m_pcRateCtrl->getRCPic()->getLCUEstQP(estLambda, pcSlice->getSliceQp());
				}

				estQP = Clip3(-pcSlice->getSPS()->getQpBDOffset(CHANNEL_TYPE_LUMA), MAX_QP, estQP);

				m_pcRdCost->setLambda(estLambda, pcSlice->getSPS()->getBitDepths());

#if RDOQ_CHROMA_LAMBDA
				// set lambda for RDOQ
				const Double chromaLambda = estLambda / m_pcRdCost->getChromaWeight();
				const Double lambdaArray[MAX_NUM_COMPONENT] = { estLambda, chromaLambda, chromaLambda };
				m_pcTrQuant->setLambdas(lambdaArray);
#else
				m_pcTrQuant->setLambda(estLambda);
#endif
			}

			m_pcRateCtrl->setRCQP(estQP);
#if ADAPTIVE_QP_SELECTION
			pCtu->getSlice()->setSliceQpBase(estQP);
#endif
		}

		// run CTU trial encoder
		m_pcCuEncoder->compressCtu(pCtu);


		// All CTU decisions have now been made. Restore entropy coder to an initial stage, ready to make a true encode,
		// which will result in the state of the contexts being correct. It will also count up the number of bits coded,
		// which is used if there is a limit of the number of bytes per slice-segment.

		m_pcEntropyCoder->setEntropyCoder(m_pppcRDSbacCoder[0][CI_CURR_BEST]);
		m_pcEntropyCoder->setBitstream(&tempBitCounter);
		pRDSbacCoder->setBinCountingEnableFlag(true);
		m_pppcRDSbacCoder[0][CI_CURR_BEST]->resetBits();
		pRDSbacCoder->setBinsCoded(0);

		// encode CTU and calculate the true bit counters.
		m_pcCuEncoder->encodeCtu(pCtu);


		pRDSbacCoder->setBinCountingEnableFlag(false);

		const Int numberOfWrittenBits = m_pcEntropyCoder->getNumberOfWrittenBits();

		// Calculate if this CTU puts us over slice bit size.
		// cannot terminate if current slice/slice-segment would be 0 Ctu in size,
		const UInt validEndOfSliceCtuTsAddr = ctuTsAddr + (ctuTsAddr == startCtuTsAddr ? 1 : 0);
		// Set slice end parameter
		if (pcSlice->getSliceMode() == FIXED_NUMBER_OF_BYTES && pcSlice->getSliceBits() + numberOfWrittenBits > (pcSlice->getSliceArgument() << 3))
		{
			pcSlice->setSliceSegmentCurEndCtuTsAddr(validEndOfSliceCtuTsAddr);
			pcSlice->setSliceCurEndCtuTsAddr(validEndOfSliceCtuTsAddr);
			boundingCtuTsAddr = validEndOfSliceCtuTsAddr;
		}
		else if ((!bCompressEntireSlice) && pcSlice->getSliceSegmentMode() == FIXED_NUMBER_OF_BYTES && pcSlice->getSliceSegmentBits() + numberOfWrittenBits > (pcSlice->getSliceSegmentArgument() << 3))
		{
			pcSlice->setSliceSegmentCurEndCtuTsAddr(validEndOfSliceCtuTsAddr);
			boundingCtuTsAddr = validEndOfSliceCtuTsAddr;
		}

		if (boundingCtuTsAddr <= ctuTsAddr)
		{
			break;
		}

		pcSlice->setSliceBits((UInt)(pcSlice->getSliceBits() + numberOfWrittenBits));
		pcSlice->setSliceSegmentBits(pcSlice->getSliceSegmentBits() + numberOfWrittenBits);

		// Store probabilities of second CTU in line into buffer - used only if wavefront-parallel-processing is enabled.
		if (ctuXPosInCtus == tileXPosInCtus + 1 && m_pcCfg->getWaveFrontsynchro())
		{
			m_entropyCodingSyncContextState.loadContexts(m_pppcRDSbacCoder[0][CI_CURR_BEST]);
		}


		if (m_pcCfg->getUseRateCtrl())
		{
			Int actualQP = g_RCInvalidQPValue;
			Double actualLambda = m_pcRdCost->getLambda();
			Int actualBits = pCtu->getTotalBits();
			Int numberOfEffectivePixels = 0;
			for (Int idx = 0; idx < pcPic->getNumPartitionsInCtu(); idx++)
			{
				if (pCtu->getPredictionMode(idx) != NUMBER_OF_PREDICTION_MODES && (!pCtu->isSkipped(idx)))
				{
					numberOfEffectivePixels = numberOfEffectivePixels + 16;
					break;
				}
			}

			if (numberOfEffectivePixels == 0)
			{
				actualQP = g_RCInvalidQPValue;
			}
			else
			{
				actualQP = pCtu->getQP(0);
			}
			m_pcRdCost->setLambda(oldLambda, pcSlice->getSPS()->getBitDepths());
			m_pcRateCtrl->getRCPic()->updateAfterCTU(m_pcRateCtrl->getRCPic()->getLCUCoded(), actualBits, actualQP, actualLambda,
				pCtu->getSlice()->getSliceType() == I_SLICE ? 0 : m_pcCfg->getLCULevelRC());
		}

		m_uiPicTotalBits += pCtu->getTotalBits();
		m_dPicRdCost += pCtu->getTotalCost();
		m_uiPicDist += pCtu->getTotalDistortion();
	}

	// store context state at the end of this slice-segment, in case the next slice is a dependent slice and continues using the CABAC contexts.
	if (pcSlice->getPPS()->getDependentSliceSegmentsEnabledFlag())
	{
		m_lastSliceSegmentEndContextState.loadContexts(m_pppcRDSbacCoder[0][CI_CURR_BEST]);//ctx end of dep.slice
	}

	// stop use of temporary bit counter object.
	m_pppcRDSbacCoder[0][CI_CURR_BEST]->setBitstream(NULL);
	m_pcRDGoOnSbacCoder->setBitstream(NULL); // stop use of tempBitCounter.

	// TODO: optimise cabac_init during compress slice to improve multi-slice operation
	//if (pcSlice->getPPS()->getCabacInitPresentFlag() && !pcSlice->getPPS()->getDependentSliceSegmentsEnabledFlag())
	//{
	//  m_encCABACTableIdx = m_pcEntropyCoder->determineCabacInitIdx();
	//}
	//else
	//{
	//  m_encCABACTableIdx = pcSlice->getSliceType();
	//}
}