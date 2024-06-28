Void TEncGOP::compressGOP_train(Double *Feature0, Double *Feature1, Double *Feature2,
	Int *Truth0, Int *Truth1, Int *Truth2,
	Int frameSize0, Int frameSize1, Int frameSize2, Int feature_num_level0, Int feature_num_level1, Int feature_num_level2,
	Int iPOCLast, Int iNumPicRcvd, TComList<TComPic*>& rcListPic,
	TComList<TComPicYuv*>& rcListPicYuvRecOut, std::list<AccessUnit>& accessUnitsInGOP,
	Bool isField, Bool isTff, const InputColourSpaceConversion snr_conversion, const Bool printFrameMSE)
{
	// TODO: Split this function up.

	TComPic*        pcPic = NULL;
	TComPicYuv*     pcPicYuvRecOut;
	TComSlice*      pcSlice;
	TComOutputBitstream  *pcBitstreamRedirect;
	pcBitstreamRedirect = new TComOutputBitstream;
	AccessUnit::iterator  itLocationToPushSliceHeaderNALU; // used to store location where NALU containing slice header is to be inserted

	xInitGOP(iPOCLast, iNumPicRcvd, isField);

	m_iNumPicCoded = 0;
	SEIMessages leadingSeiMessages;
	SEIMessages nestedSeiMessages;
	SEIMessages duInfoSeiMessages;
	SEIMessages trailingSeiMessages;
	std::deque<DUData> duData;
	SEIDecodingUnitInfo decodingUnitInfoSEI;

	EfficientFieldIRAPMapping effFieldIRAPMap;
	if (m_pcCfg->getEfficientFieldIRAPEnabled())
	{
		effFieldIRAPMap.initialize(isField, m_iGopSize, iPOCLast, iNumPicRcvd, m_iLastIDR, this, m_pcCfg);
	}

	// reset flag indicating whether pictures have been encoded
	for (Int iGOPid = 0; iGOPid < m_iGopSize; iGOPid++)
	{
		m_pcCfg->setEncodedFlag(iGOPid, false);
	}

	for (Int iGOPid = 0; iGOPid < m_iGopSize; iGOPid++)
	{
		if (m_pcCfg->getEfficientFieldIRAPEnabled())
		{
			iGOPid = effFieldIRAPMap.adjustGOPid(iGOPid);
		}

		//-- For time output for each slice
		clock_t iBeforeTime = clock();

		UInt uiColDir = calculateCollocatedFromL1Flag(m_pcCfg, iGOPid, m_iGopSize);

		/////////////////////////////////////////////////////////////////////////////////////////////////// Initial to start encoding
		Int iTimeOffset;
		Int pocCurr;

		if (iPOCLast == 0) //case first frame or first top field
		{
			pocCurr = 0;
			iTimeOffset = 1;
		}
		else if (iPOCLast == 1 && isField) //case first bottom field, just like the first frame, the poc computation is not right anymore, we set the right value
		{
			pocCurr = 1;
			iTimeOffset = 1;
		}
		else
		{
			pocCurr = iPOCLast - iNumPicRcvd + m_pcCfg->getGOPEntry(iGOPid).m_POC - ((isField && m_iGopSize > 1) ? 1 : 0);
			iTimeOffset = m_pcCfg->getGOPEntry(iGOPid).m_POC;
		}

		if (pocCurr >= m_pcCfg->getFramesToBeEncoded())
		{
			if (m_pcCfg->getEfficientFieldIRAPEnabled())
			{
				iGOPid = effFieldIRAPMap.restoreGOPid(iGOPid);
			}
			continue;
		}

		if (getNalUnitType(pocCurr, m_iLastIDR, isField) == NAL_UNIT_CODED_SLICE_IDR_W_RADL || getNalUnitType(pocCurr, m_iLastIDR, isField) == NAL_UNIT_CODED_SLICE_IDR_N_LP)
		{
			m_iLastIDR = pocCurr;
		}
		// start a new access unit: create an entry in the list of output access units
		accessUnitsInGOP.push_back(AccessUnit());
		AccessUnit& accessUnit = accessUnitsInGOP.back();
		xGetBuffer(rcListPic, rcListPicYuvRecOut, iNumPicRcvd, iTimeOffset, pcPic, pcPicYuvRecOut, pocCurr, isField);

		//  Slice data initialization
		pcPic->clearSliceBuffer();
		pcPic->allocateNewSlice();
		m_pcSliceEncoder->setSliceIdx(0);
		pcPic->setCurrSliceIdx(0);

		m_pcSliceEncoder->initEncSlice(pcPic, iPOCLast, pocCurr, iGOPid, pcSlice, isField);

		//Set Frame/Field coding
		pcSlice->getPic()->setField(isField);

		pcSlice->setLastIDR(m_iLastIDR);
		pcSlice->setSliceIdx(0);
		//set default slice level flag to the same as SPS level flag
		pcSlice->setLFCrossSliceBoundaryFlag(pcSlice->getPPS()->getLoopFilterAcrossSlicesEnabledFlag());

		if (pcSlice->getSliceType() == B_SLICE && m_pcCfg->getGOPEntry(iGOPid).m_sliceType == 'P')
		{
			pcSlice->setSliceType(P_SLICE);
		}
		if (pcSlice->getSliceType() == B_SLICE && m_pcCfg->getGOPEntry(iGOPid).m_sliceType == 'I')
		{
			pcSlice->setSliceType(I_SLICE);
		}

		// Set the nal unit type
		pcSlice->setNalUnitType(getNalUnitType(pocCurr, m_iLastIDR, isField));
		if (pcSlice->getTemporalLayerNonReferenceFlag())
		{
			if (pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_TRAIL_R &&
				!(m_iGopSize == 1 && pcSlice->getSliceType() == I_SLICE))
				// Add this condition to avoid POC issues with encoder_intra_main.cfg configuration (see #1127 in bug tracker)
			{
				pcSlice->setNalUnitType(NAL_UNIT_CODED_SLICE_TRAIL_N);
			}
			if (pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_RADL_R)
			{
				pcSlice->setNalUnitType(NAL_UNIT_CODED_SLICE_RADL_N);
			}
			if (pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_RASL_R)
			{
				pcSlice->setNalUnitType(NAL_UNIT_CODED_SLICE_RASL_N);
			}
		}

		if (m_pcCfg->getEfficientFieldIRAPEnabled())
		{
			if (pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_BLA_W_LP
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_BLA_W_RADL
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_BLA_N_LP
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_IDR_W_RADL
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_IDR_N_LP
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_CRA)  // IRAP picture
			{
				m_associatedIRAPType = pcSlice->getNalUnitType();
				m_associatedIRAPPOC = pocCurr;
			}
			pcSlice->setAssociatedIRAPType(m_associatedIRAPType);
			pcSlice->setAssociatedIRAPPOC(m_associatedIRAPPOC);
		}
		// Do decoding refresh marking if any
		pcSlice->decodingRefreshMarking(m_pocCRA, m_bRefreshPending, rcListPic, m_pcCfg->getEfficientFieldIRAPEnabled());
		m_pcEncTop->selectReferencePictureSet(pcSlice, pocCurr, iGOPid);
		pcSlice->getRPS()->setNumberOfLongtermPictures(0);
		if (!m_pcCfg->getEfficientFieldIRAPEnabled())
		{
			if (pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_BLA_W_LP
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_BLA_W_RADL
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_BLA_N_LP
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_IDR_W_RADL
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_IDR_N_LP
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_CRA)  // IRAP picture
			{
				m_associatedIRAPType = pcSlice->getNalUnitType();
				m_associatedIRAPPOC = pocCurr;
			}
			pcSlice->setAssociatedIRAPType(m_associatedIRAPType);
			pcSlice->setAssociatedIRAPPOC(m_associatedIRAPPOC);
		}

		if ((pcSlice->checkThatAllRefPicsAreAvailable(rcListPic, pcSlice->getRPS(), false, m_iLastRecoveryPicPOC, m_pcCfg->getDecodingRefreshType() == 3) != 0) || (pcSlice->isIRAP())
			|| (m_pcCfg->getEfficientFieldIRAPEnabled() && isField && pcSlice->getAssociatedIRAPType() >= NAL_UNIT_CODED_SLICE_BLA_W_LP && pcSlice->getAssociatedIRAPType() <= NAL_UNIT_CODED_SLICE_CRA && pcSlice->getAssociatedIRAPPOC() == pcSlice->getPOC() + 1)
			)
		{
			pcSlice->createExplicitReferencePictureSetFromReference(rcListPic, pcSlice->getRPS(), pcSlice->isIRAP(), m_iLastRecoveryPicPOC, m_pcCfg->getDecodingRefreshType() == 3, m_pcCfg->getEfficientFieldIRAPEnabled());
		}

		pcSlice->applyReferencePictureSet(rcListPic, pcSlice->getRPS());

		if (pcSlice->getTLayer() > 0
			&& !(pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_RADL_N     // Check if not a leading picture
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_RADL_R
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_RASL_N
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_RASL_R)
			)
		{
			if (pcSlice->isTemporalLayerSwitchingPoint(rcListPic) || pcSlice->getSPS()->getTemporalIdNestingFlag())
			{
				if (pcSlice->getTemporalLayerNonReferenceFlag())
				{
					pcSlice->setNalUnitType(NAL_UNIT_CODED_SLICE_TSA_N);
				}
				else
				{
					pcSlice->setNalUnitType(NAL_UNIT_CODED_SLICE_TSA_R);
				}
			}
			else if (pcSlice->isStepwiseTemporalLayerSwitchingPointCandidate(rcListPic))
			{
				Bool isSTSA = true;
				for (Int ii = iGOPid + 1; (ii < m_pcCfg->getGOPSize() && isSTSA == true); ii++)
				{
					Int lTid = m_pcCfg->getGOPEntry(ii).m_temporalId;
					if (lTid == pcSlice->getTLayer())
					{
						const TComReferencePictureSet* nRPS = pcSlice->getSPS()->getRPSList()->getReferencePictureSet(ii);
						for (Int jj = 0; jj < nRPS->getNumberOfPictures(); jj++)
						{
							if (nRPS->getUsed(jj))
							{
								Int tPoc = m_pcCfg->getGOPEntry(ii).m_POC + nRPS->getDeltaPOC(jj);
								Int kk = 0;
								for (kk = 0; kk < m_pcCfg->getGOPSize(); kk++)
								{
									if (m_pcCfg->getGOPEntry(kk).m_POC == tPoc)
									{
										break;
									}
								}
								Int tTid = m_pcCfg->getGOPEntry(kk).m_temporalId;
								if (tTid >= pcSlice->getTLayer())
								{
									isSTSA = false;
									break;
								}
							}
						}
					}
				}
				if (isSTSA == true)
				{
					if (pcSlice->getTemporalLayerNonReferenceFlag())
					{
						pcSlice->setNalUnitType(NAL_UNIT_CODED_SLICE_STSA_N);
					}
					else
					{
						pcSlice->setNalUnitType(NAL_UNIT_CODED_SLICE_STSA_R);
					}
				}
			}
		}
		arrangeLongtermPicturesInRPS(pcSlice, rcListPic);
		TComRefPicListModification* refPicListModification = pcSlice->getRefPicListModification();
		refPicListModification->setRefPicListModificationFlagL0(0);
		refPicListModification->setRefPicListModificationFlagL1(0);
		pcSlice->setNumRefIdx(REF_PIC_LIST_0, min(m_pcCfg->getGOPEntry(iGOPid).m_numRefPicsActive, pcSlice->getRPS()->getNumberOfPictures()));
		pcSlice->setNumRefIdx(REF_PIC_LIST_1, min(m_pcCfg->getGOPEntry(iGOPid).m_numRefPicsActive, pcSlice->getRPS()->getNumberOfPictures()));

		//  Set reference list
		pcSlice->setRefPicList(rcListPic);

		//  Slice info. refinement
		if ((pcSlice->getSliceType() == B_SLICE) && (pcSlice->getNumRefIdx(REF_PIC_LIST_1) == 0))
		{
			pcSlice->setSliceType(P_SLICE);
		}
		pcSlice->setEncCABACTableIdx(m_pcSliceEncoder->getEncCABACTableIdx());

		if (pcSlice->getSliceType() == B_SLICE)
		{
			pcSlice->setColFromL0Flag(1 - uiColDir);
			Bool bLowDelay = true;
			Int  iCurrPOC = pcSlice->getPOC();
			Int iRefIdx = 0;

			for (iRefIdx = 0; iRefIdx < pcSlice->getNumRefIdx(REF_PIC_LIST_0) && bLowDelay; iRefIdx++)
			{
				if (pcSlice->getRefPic(REF_PIC_LIST_0, iRefIdx)->getPOC() > iCurrPOC)
				{
					bLowDelay = false;
				}
			}
			for (iRefIdx = 0; iRefIdx < pcSlice->getNumRefIdx(REF_PIC_LIST_1) && bLowDelay; iRefIdx++)
			{
				if (pcSlice->getRefPic(REF_PIC_LIST_1, iRefIdx)->getPOC() > iCurrPOC)
				{
					bLowDelay = false;
				}
			}

			pcSlice->setCheckLDC(bLowDelay);
		}
		else
		{
			pcSlice->setCheckLDC(true);
		}

		uiColDir = 1 - uiColDir;

		//-------------------------------------------------------------
		pcSlice->setRefPOCList();

		pcSlice->setList1IdxToList0Idx();

		if (m_pcEncTop->getTMVPModeId() == 2)
		{
			if (iGOPid == 0) // first picture in SOP (i.e. forward B)
			{
				pcSlice->setEnableTMVPFlag(0);
			}
			else
			{
				// Note: pcSlice->getColFromL0Flag() is assumed to be always 0 and getcolRefIdx() is always 0.
				pcSlice->setEnableTMVPFlag(1);
			}
		}
		else if (m_pcEncTop->getTMVPModeId() == 1)
		{
			pcSlice->setEnableTMVPFlag(1);
		}
		else
		{
			pcSlice->setEnableTMVPFlag(0);
		}
		/////////////////////////////////////////////////////////////////////////////////////////////////// Compress a slice
		//  Slice compression
		if (m_pcCfg->getUseASR())
		{
			m_pcSliceEncoder->setSearchRange(pcSlice);
		}

		Bool bGPBcheck = false;
		if (pcSlice->getSliceType() == B_SLICE)
		{
			if (pcSlice->getNumRefIdx(RefPicList(0)) == pcSlice->getNumRefIdx(RefPicList(1)))
			{
				bGPBcheck = true;
				Int i;
				for (i = 0; i < pcSlice->getNumRefIdx(RefPicList(1)); i++)
				{
					if (pcSlice->getRefPOC(RefPicList(1), i) != pcSlice->getRefPOC(RefPicList(0), i))
					{
						bGPBcheck = false;
						break;
					}
				}
			}
		}
		if (bGPBcheck)
		{
			pcSlice->setMvdL1ZeroFlag(true);
		}
		else
		{
			pcSlice->setMvdL1ZeroFlag(false);
		}
		pcPic->getSlice(pcSlice->getSliceIdx())->setMvdL1ZeroFlag(pcSlice->getMvdL1ZeroFlag());


		Double lambda = 0.0;
		Int actualHeadBits = 0;
		Int actualTotalBits = 0;
		Int estimatedBits = 0;
		Int tmpBitsBeforeWriting = 0;
		if (m_pcCfg->getUseRateCtrl()) // TODO: does this work with multiple slices and slice-segments?
		{
			Int frameLevel = m_pcRateCtrl->getRCSeq()->getGOPID2Level(iGOPid);
			if (pcPic->getSlice(0)->getSliceType() == I_SLICE)
			{
				frameLevel = 0;
			}
			m_pcRateCtrl->initRCPic(frameLevel);
			estimatedBits = m_pcRateCtrl->getRCPic()->getTargetBits();

			Int sliceQP = m_pcCfg->getInitialQP();
			if ((pcSlice->getPOC() == 0 && m_pcCfg->getInitialQP() > 0) || (frameLevel == 0 && m_pcCfg->getForceIntraQP())) // QP is specified
			{
				Int    NumberBFrames = (m_pcCfg->getGOPSize() - 1);
				Double dLambda_scale = 1.0 - Clip3(0.0, 0.5, 0.05*(Double)NumberBFrames);
				Double dQPFactor = 0.57*dLambda_scale;
				Int    SHIFT_QP = 12;
				Int    bitdepth_luma_qp_scale = 0;
				Double qp_temp = (Double)sliceQP + bitdepth_luma_qp_scale - SHIFT_QP;
				lambda = dQPFactor * pow(2.0, qp_temp / 3.0);
			}
			else if (frameLevel == 0)   // intra case, but use the model
			{
				m_pcSliceEncoder->calCostSliceI(pcPic); // TODO: This only analyses the first slice segment - what about the others?

				if (m_pcCfg->getIntraPeriod() != 1)   // do not refine allocated bits for all intra case
				{
					Int bits = m_pcRateCtrl->getRCSeq()->getLeftAverageBits();
					bits = m_pcRateCtrl->getRCPic()->getRefineBitsForIntra(bits);
					if (bits < 200)
					{
						bits = 200;
					}
					m_pcRateCtrl->getRCPic()->setTargetBits(bits);
				}

				list<TEncRCPic*> listPreviousPicture = m_pcRateCtrl->getPicList();
				m_pcRateCtrl->getRCPic()->getLCUInitTargetBits();
				lambda = m_pcRateCtrl->getRCPic()->estimatePicLambda(listPreviousPicture, pcSlice->getSliceType());
				sliceQP = m_pcRateCtrl->getRCPic()->estimatePicQP(lambda, listPreviousPicture);
			}
			else    // normal case
			{
				list<TEncRCPic*> listPreviousPicture = m_pcRateCtrl->getPicList();
				lambda = m_pcRateCtrl->getRCPic()->estimatePicLambda(listPreviousPicture, pcSlice->getSliceType());
				sliceQP = m_pcRateCtrl->getRCPic()->estimatePicQP(lambda, listPreviousPicture);
			}

			sliceQP = Clip3(-pcSlice->getSPS()->getQpBDOffset(CHANNEL_TYPE_LUMA), MAX_QP, sliceQP);
			m_pcRateCtrl->getRCPic()->setPicEstQP(sliceQP);

			m_pcSliceEncoder->resetQP(pcPic, sliceQP, lambda);
		}

		UInt uiNumSliceSegments = 1;

		// Allocate some coders, now the number of tiles are known.
		const Int numSubstreamsColumns = (pcSlice->getPPS()->getNumTileColumnsMinus1() + 1);
		const Int numSubstreamRows = pcSlice->getPPS()->getEntropyCodingSyncEnabledFlag() ? pcPic->getFrameHeightInCtus() : (pcSlice->getPPS()->getNumTileRowsMinus1() + 1);
		const Int numSubstreams = numSubstreamRows * numSubstreamsColumns;
		std::vector<TComOutputBitstream> substreamsOut(numSubstreams);

		// now compress (trial encode) the various slice segments (slices, and dependent slices)
		{
			const UInt numberOfCtusInFrame = pcPic->getPicSym()->getNumberOfCtusInFrame();
			pcSlice->setSliceCurStartCtuTsAddr(0);
			pcSlice->setSliceSegmentCurStartCtuTsAddr(0);

			for (UInt nextCtuTsAddr = 0; nextCtuTsAddr < numberOfCtusInFrame; )
			{
				m_pcSliceEncoder->precompressSlice_train(Feature0, Feature1, Feature2,
					Truth0, Truth1, Truth2,
					frameSize0, frameSize1, frameSize2, feature_num_level0, feature_num_level1, feature_num_level2, iGOPid,
					pcPic);
				m_pcSliceEncoder->compressSlice_train(Feature0, Feature1, Feature2,
					Truth0, Truth1, Truth2,
					frameSize0, frameSize1, frameSize2, feature_num_level0, feature_num_level1, feature_num_level2, iGOPid,
					pcPic, false);

				const UInt curSliceSegmentEnd = pcSlice->getSliceSegmentCurEndCtuTsAddr();
				if (curSliceSegmentEnd < numberOfCtusInFrame)
				{
					const Bool bNextSegmentIsDependentSlice = curSliceSegmentEnd < pcSlice->getSliceCurEndCtuTsAddr();
					const UInt sliceBits = pcSlice->getSliceBits();
					pcPic->allocateNewSlice();
					// prepare for next slice
					pcPic->setCurrSliceIdx(uiNumSliceSegments);
					m_pcSliceEncoder->setSliceIdx(uiNumSliceSegments);
					pcSlice = pcPic->getSlice(uiNumSliceSegments);
					assert(pcSlice->getPPS() != 0);
					pcSlice->copySliceInfo(pcPic->getSlice(uiNumSliceSegments - 1));
					pcSlice->setSliceIdx(uiNumSliceSegments);
					if (bNextSegmentIsDependentSlice)
					{
						pcSlice->setSliceBits(sliceBits);
					}
					else
					{
						pcSlice->setSliceCurStartCtuTsAddr(curSliceSegmentEnd);
						pcSlice->setSliceBits(0);
					}
					pcSlice->setDependentSliceSegmentFlag(bNextSegmentIsDependentSlice);
					pcSlice->setSliceSegmentCurStartCtuTsAddr(curSliceSegmentEnd);
					// TODO: optimise cabac_init during compress slice to improve multi-slice operation
					// pcSlice->setEncCABACTableIdx(m_pcSliceEncoder->getEncCABACTableIdx());
					uiNumSliceSegments++;
				}
				nextCtuTsAddr = curSliceSegmentEnd;
			}
		}

		duData.clear();
		pcSlice = pcPic->getSlice(0);

		// SAO parameter estimation using non-deblocked pixels for CTU bottom and right boundary areas
		if (pcSlice->getSPS()->getUseSAO() && m_pcCfg->getSaoCtuBoundary())
		{
			m_pcSAO->getPreDBFStatistics(pcPic);
		}

		//-- Loop filter
		Bool bLFCrossTileBoundary = pcSlice->getPPS()->getLoopFilterAcrossTilesEnabledFlag();
		m_pcLoopFilter->setCfg(bLFCrossTileBoundary);
		if (m_pcCfg->getDeblockingFilterMetric())
		{
			applyDeblockingFilterMetric(pcPic, uiNumSliceSegments);
		}
		m_pcLoopFilter->loopFilterPic(pcPic);

		/////////////////////////////////////////////////////////////////////////////////////////////////// File writing
		// Set entropy coder
		m_pcEntropyCoder->setEntropyCoder(m_pcCavlcCoder);

		if (m_bSeqFirst)
		{
			// write various parameter sets
			actualTotalBits += xWriteParameterSets(accessUnit, pcSlice);

			// create prefix SEI messages at the beginning of the sequence
			assert(leadingSeiMessages.empty());
			xCreateIRAPLeadingSEIMessages(leadingSeiMessages, pcSlice->getSPS(), pcSlice->getPPS());

			m_bSeqFirst = false;
		}

		// reset presence of BP SEI indication
		m_bufferingPeriodSEIPresentInAU = false;
		// create prefix SEI associated with a picture
		xCreatePerPictureSEIMessages(iGOPid, leadingSeiMessages, nestedSeiMessages, pcSlice);

		/* use the main bitstream buffer for storing the marshalled picture */
		m_pcEntropyCoder->setBitstream(NULL);

		pcSlice = pcPic->getSlice(0);

		if (pcSlice->getSPS()->getUseSAO())
		{
			Bool sliceEnabled[MAX_NUM_COMPONENT];
			TComBitCounter tempBitCounter;
			tempBitCounter.resetBits();
			m_pcEncTop->getRDGoOnSbacCoder()->setBitstream(&tempBitCounter);
			m_pcSAO->initRDOCabacCoder(m_pcEncTop->getRDGoOnSbacCoder(), pcSlice);
			m_pcSAO->SAOProcess(pcPic, sliceEnabled, pcPic->getSlice(0)->getLambdas(), m_pcCfg->getTestSAODisableAtPictureLevel(), m_pcCfg->getSaoEncodingRate(), m_pcCfg->getSaoEncodingRateChroma(), m_pcCfg->getSaoCtuBoundary());
			m_pcSAO->PCMLFDisableProcess(pcPic);
			m_pcEncTop->getRDGoOnSbacCoder()->setBitstream(NULL);

			//assign SAO slice header
			for (Int s = 0; s < uiNumSliceSegments; s++)
			{
				pcPic->getSlice(s)->setSaoEnabledFlag(CHANNEL_TYPE_LUMA, sliceEnabled[COMPONENT_Y]);
				assert(sliceEnabled[COMPONENT_Cb] == sliceEnabled[COMPONENT_Cr]);
				pcPic->getSlice(s)->setSaoEnabledFlag(CHANNEL_TYPE_CHROMA, sliceEnabled[COMPONENT_Cb]);
			}
		}

		// pcSlice is currently slice 0.
		std::size_t binCountsInNalUnits = 0; // For implementation of cabac_zero_word stuffing (section 7.4.3.10)
		std::size_t numBytesInVclNalUnits = 0; // For implementation of cabac_zero_word stuffing (section 7.4.3.10)

		for (UInt sliceSegmentStartCtuTsAddr = 0, sliceIdxCount = 0; sliceSegmentStartCtuTsAddr < pcPic->getPicSym()->getNumberOfCtusInFrame(); sliceIdxCount++, sliceSegmentStartCtuTsAddr = pcSlice->getSliceSegmentCurEndCtuTsAddr())
		{
			pcSlice = pcPic->getSlice(sliceIdxCount);
			if (sliceIdxCount > 0 && pcSlice->getSliceType() != I_SLICE)
			{
				pcSlice->checkColRefIdx(sliceIdxCount, pcPic);
			}
			pcPic->setCurrSliceIdx(sliceIdxCount);
			m_pcSliceEncoder->setSliceIdx(sliceIdxCount);

			pcSlice->setRPS(pcPic->getSlice(0)->getRPS());
			pcSlice->setRPSidx(pcPic->getSlice(0)->getRPSidx());

			for (UInt ui = 0; ui < numSubstreams; ui++)
			{
				substreamsOut[ui].clear();
			}

			m_pcEntropyCoder->setEntropyCoder(m_pcCavlcCoder);
			m_pcEntropyCoder->resetEntropy(pcSlice);
			/* start slice NALunit */
			OutputNALUnit nalu(pcSlice->getNalUnitType(), pcSlice->getTLayer());
			m_pcEntropyCoder->setBitstream(&nalu.m_Bitstream);

			pcSlice->setNoRaslOutputFlag(false);
			if (pcSlice->isIRAP())
			{
				if (pcSlice->getNalUnitType() >= NAL_UNIT_CODED_SLICE_BLA_W_LP && pcSlice->getNalUnitType() <= NAL_UNIT_CODED_SLICE_IDR_N_LP)
				{
					pcSlice->setNoRaslOutputFlag(true);
				}
				//the inference for NoOutputPriorPicsFlag
				// KJS: This cannot happen at the encoder
				if (!m_bFirst && pcSlice->isIRAP() && pcSlice->getNoRaslOutputFlag())
				{
					if (pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_CRA)
					{
						pcSlice->setNoOutputPriorPicsFlag(true);
					}
				}
			}

			pcSlice->setEncCABACTableIdx(m_pcSliceEncoder->getEncCABACTableIdx());

			tmpBitsBeforeWriting = m_pcEntropyCoder->getNumberOfWrittenBits();
			m_pcEntropyCoder->encodeSliceHeader(pcSlice);
			actualHeadBits += (m_pcEntropyCoder->getNumberOfWrittenBits() - tmpBitsBeforeWriting);

			pcSlice->setFinalized(true);

			pcSlice->clearSubstreamSizes();
			{
				UInt numBinsCoded = 0;
				m_pcSliceEncoder->encodeSlice(pcPic, &(substreamsOut[0]), numBinsCoded);
				binCountsInNalUnits += numBinsCoded;
			}

			{
				// Construct the final bitstream by concatenating substreams.
				// The final bitstream is either nalu.m_Bitstream or pcBitstreamRedirect;
				// Complete the slice header info.
				m_pcEntropyCoder->setEntropyCoder(m_pcCavlcCoder);
				m_pcEntropyCoder->setBitstream(&nalu.m_Bitstream);
				m_pcEntropyCoder->encodeTilesWPPEntryPoint(pcSlice);

				// Append substreams...
				TComOutputBitstream *pcOut = pcBitstreamRedirect;
				const Int numZeroSubstreamsAtStartOfSlice = pcPic->getSubstreamForCtuAddr(pcSlice->getSliceSegmentCurStartCtuTsAddr(), false, pcSlice);
				const Int numSubstreamsToCode = pcSlice->getNumberOfSubstreamSizes() + 1;
				for (UInt ui = 0; ui < numSubstreamsToCode; ui++)
				{
					pcOut->addSubstream(&(substreamsOut[ui + numZeroSubstreamsAtStartOfSlice]));
				}
			}

			// If current NALU is the first NALU of slice (containing slice header) and more NALUs exist (due to multiple dependent slices) then buffer it.
			// If current NALU is the last NALU of slice and a NALU was buffered, then (a) Write current NALU (b) Update an write buffered NALU at approproate location in NALU list.
			Bool bNALUAlignedWrittenToList = false; // used to ensure current NALU is not written more than once to the NALU list.
			xAttachSliceDataToNalUnit(nalu, pcBitstreamRedirect);
			accessUnit.push_back(new NALUnitEBSP(nalu));
			actualTotalBits += UInt(accessUnit.back()->m_nalUnitData.str().size()) * 8;
			numBytesInVclNalUnits += (std::size_t)(accessUnit.back()->m_nalUnitData.str().size());
			bNALUAlignedWrittenToList = true;

			if (!bNALUAlignedWrittenToList)
			{
				nalu.m_Bitstream.writeAlignZero();
				accessUnit.push_back(new NALUnitEBSP(nalu));
			}

			if ((m_pcCfg->getPictureTimingSEIEnabled() || m_pcCfg->getDecodingUnitInfoSEIEnabled()) &&
				(pcSlice->getSPS()->getVuiParametersPresentFlag()) &&
				((pcSlice->getSPS()->getVuiParameters()->getHrdParameters()->getNalHrdParametersPresentFlag())
					|| (pcSlice->getSPS()->getVuiParameters()->getHrdParameters()->getVclHrdParametersPresentFlag())) &&
					(pcSlice->getSPS()->getVuiParameters()->getHrdParameters()->getSubPicCpbParamsPresentFlag()))
			{
				UInt numNalus = 0;
				UInt numRBSPBytes = 0;
				for (AccessUnit::const_iterator it = accessUnit.begin(); it != accessUnit.end(); it++)
				{
					numRBSPBytes += UInt((*it)->m_nalUnitData.str().size());
					numNalus++;
				}
				duData.push_back(DUData());
				duData.back().accumBitsDU = (numRBSPBytes << 3);
				duData.back().accumNalsDU = numNalus;
			}
		} // end iteration over slices

		// cabac_zero_words processing
		cabac_zero_word_padding(pcSlice, pcPic, binCountsInNalUnits, numBytesInVclNalUnits, accessUnit.back()->m_nalUnitData, m_pcCfg->getCabacZeroWordPaddingEnabled());

		pcPic->compressMotion();

		//-- For time output for each slice
		Double dEncTime = (Double)(clock() - iBeforeTime) / CLOCKS_PER_SEC;

		std::string digestStr;
		if (m_pcCfg->getDecodedPictureHashSEIEnabled())
		{
			SEIDecodedPictureHash *decodedPictureHashSei = new SEIDecodedPictureHash();
			m_seiEncoder.initDecodedPictureHashSEI(decodedPictureHashSei, pcPic, digestStr, pcSlice->getSPS()->getBitDepths());
			trailingSeiMessages.push_back(decodedPictureHashSei);
		}
		xWriteTrailingSEIMessages(trailingSeiMessages, accessUnit, pcSlice->getTLayer(), pcSlice->getSPS());

		m_pcCfg->setEncodedFlag(iGOPid, true);

		xCalculateAddPSNRs(isField, isTff, iGOPid, pcPic, accessUnit, rcListPic, dEncTime, snr_conversion, printFrameMSE);

		if (!digestStr.empty())
		{
			if (m_pcCfg->getDecodedPictureHashSEIEnabled() == 1)
			{
				printf(" [MD5:%s]", digestStr.c_str());
			}
			else if (m_pcCfg->getDecodedPictureHashSEIEnabled() == 2)
			{
				printf(" [CRC:%s]", digestStr.c_str());
			}
			else if (m_pcCfg->getDecodedPictureHashSEIEnabled() == 3)
			{
				printf(" [Checksum:%s]", digestStr.c_str());
			}
		}

		if (m_pcCfg->getUseRateCtrl())
		{
			Double avgQP = m_pcRateCtrl->getRCPic()->calAverageQP();
			Double avgLambda = m_pcRateCtrl->getRCPic()->calAverageLambda();
			if (avgLambda < 0.0)
			{
				avgLambda = lambda;
			}

			m_pcRateCtrl->getRCPic()->updateAfterPicture(actualHeadBits, actualTotalBits, avgQP, avgLambda, pcSlice->getSliceType());
			m_pcRateCtrl->getRCPic()->addToPictureLsit(m_pcRateCtrl->getPicList());

			m_pcRateCtrl->getRCSeq()->updateAfterPic(actualTotalBits);
			if (pcSlice->getSliceType() != I_SLICE)
			{
				m_pcRateCtrl->getRCGOP()->updateAfterPicture(actualTotalBits);
			}
			else    // for intra picture, the estimated bits are used to update the current status in the GOP
			{
				m_pcRateCtrl->getRCGOP()->updateAfterPicture(estimatedBits);
			}
		}

		xCreatePictureTimingSEI(m_pcCfg->getEfficientFieldIRAPEnabled() ? effFieldIRAPMap.GetIRAPGOPid() : 0, leadingSeiMessages, nestedSeiMessages, duInfoSeiMessages, pcSlice, isField, duData);
		if (m_pcCfg->getScalableNestingSEIEnabled())
		{
			xCreateScalableNestingSEI(leadingSeiMessages, nestedSeiMessages);
		}
		xWriteLeadingSEIMessages(leadingSeiMessages, duInfoSeiMessages, accessUnit, pcSlice->getTLayer(), pcSlice->getSPS(), duData);
		xWriteDuSEIMessages(duInfoSeiMessages, accessUnit, pcSlice->getTLayer(), pcSlice->getSPS(), duData);

		pcPic->getPicYuvRec()->copyToPic(pcPicYuvRecOut);

		pcPic->setReconMark(true);
		m_bFirst = false;
		m_iNumPicCoded++;
		m_totalCoded++;
		/* logging: insert a newline at end of picture period */
		printf("\n");
		fflush(stdout);

		if (m_pcCfg->getEfficientFieldIRAPEnabled())
		{
			iGOPid = effFieldIRAPMap.restoreGOPid(iGOPid);
		}
	} // iGOPid-loop

	delete pcBitstreamRedirect;

	assert((m_iNumPicCoded == iNumPicRcvd));
}