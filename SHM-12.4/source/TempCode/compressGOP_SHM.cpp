#if SVC_EXTENSION
Void TEncGOP::compressGOP(Int iPicIdInGOP, Int iPOCLast, Int iNumPicRcvd, TComList<TComPic*>& rcListPic,
#else
Void TEncGOP::compressGOP(Int iPOCLast, Int iNumPicRcvd, TComList<TComPic*>& rcListPic,
#endif
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
#if SVC_EXTENSION
		effFieldIRAPMap.initialize(isField, iPicIdInGOP, m_iGopSize, iPOCLast, iNumPicRcvd, m_iLastIDR, this, m_pcCfg);
#else
		effFieldIRAPMap.initialize(isField, m_iGopSize, iPOCLast, iNumPicRcvd, m_iLastIDR, this, m_pcCfg);
#endif
	}

	// reset flag indicating whether pictures have been encoded
	for (Int iGOPid = 0; iGOPid < m_iGopSize; iGOPid++)
	{
		m_pcCfg->setEncodedFlag(iGOPid, false);
	}
#if SVC_EXTENSION
	for (Int iGOPid = iPicIdInGOP; iGOPid < iPicIdInGOP + 1; iGOPid++)
#else
	for (Int iGOPid = 0; iGOPid < m_iGopSize; iGOPid++)
#endif
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

#if SVC_EXTENSION
		if (m_pcEncTop->getAdaptiveResolutionChange() > 0 && ((m_layerId > 0 && pocCurr < m_pcEncTop->getAdaptiveResolutionChange()) ||
			(m_layerId == 0 && pocCurr > m_pcEncTop->getAdaptiveResolutionChange())))
		{
			continue;
		}

		if (pocCurr > m_pcEncTop->getLayerSwitchOffBegin() && pocCurr < m_pcEncTop->getLayerSwitchOffEnd())
		{
			continue;
		}
#endif

		if (getNalUnitType(pocCurr, m_iLastIDR, isField) == NAL_UNIT_CODED_SLICE_IDR_W_RADL || getNalUnitType(pocCurr, m_iLastIDR, isField) == NAL_UNIT_CODED_SLICE_IDR_N_LP)
		{
			m_iLastIDR = pocCurr;
		}
		// start a new access unit: create an entry in the list of output access units
		accessUnitsInGOP.push_back(AccessUnit());
		AccessUnit& accessUnit = accessUnitsInGOP.back();
		xGetBuffer(rcListPic, rcListPicYuvRecOut, iNumPicRcvd, iTimeOffset, pcPic, pcPicYuvRecOut, pocCurr, isField);

#if REDUCED_ENCODER_MEMORY
		pcPic->prepareForReconstruction();

#endif
		//  Slice data initialization
		pcPic->clearSliceBuffer();
		pcPic->allocateNewSlice();
		m_pcSliceEncoder->setSliceIdx(0);
		pcPic->setCurrSliceIdx(0);

#if SVC_EXTENSION
		pcPic->setLayerId(m_layerId);
#endif

		m_pcSliceEncoder->initEncSlice(pcPic, iPOCLast, pocCurr, iGOPid, pcSlice, isField);

		//Set Frame/Field coding
		pcSlice->getPic()->setField(isField);

#if SVC_EXTENSION
#if SVC_POC
		pcSlice->setPocValueBeforeReset(pocCurr);
		// Check if the current picture is to be assigned as a reset picture
		determinePocResetIdc(pocCurr, pcSlice);

		Bool pocResettingFlag = false;

		if (pcSlice->getPocResetIdc())
		{
			if (pcSlice->getVPS()->getVpsPocLsbAlignedFlag())
			{
				pocResettingFlag = true;
			}
			else if (m_pcEncTop->getPocDecrementedInDPBFlag())
			{
				pocResettingFlag = false;
			}
			else
			{
				pocResettingFlag = true;
			}
		}

		// If reset, do the following steps:
		if (pocResettingFlag)
		{
			updatePocValuesOfPics(pocCurr, pcSlice);
		}
		else
		{
			// Check the base layer picture is IDR. If so, just set current POC equal to 0 (alignment of POC)
			if ((m_ppcTEncTop[0]->getGOPEncoder()->getIntraRefreshType() == 2) && m_ppcTEncTop[0]->getGOPEncoder()->getIntraRefreshInterval() >= 0 && (pocCurr % m_ppcTEncTop[0]->getGOPEncoder()->getIntraRefreshInterval() == 0))
			{
				m_pcEncTop->setPocAdjustmentValue(pocCurr);
			}

			// Just subtract POC by the current cumulative POC delta
			pcSlice->setPOC(pocCurr - m_pcEncTop->getPocAdjustmentValue());

			Int maxPocLsb = 1 << pcSlice->getSPS()->getBitsForPOC();
			pcSlice->setPocMsbVal(pcSlice->getPOC() - (pcSlice->getPOC() & (maxPocLsb - 1)));
		}
		// Update the POC of current picture, pictures in the DPB, including references inside the reference pictures
#endif

		if (m_layerId == 0 && (getNalUnitType(pocCurr, m_iLastIDR, isField) == NAL_UNIT_CODED_SLICE_IDR_W_RADL || getNalUnitType(pocCurr, m_iLastIDR, isField) == NAL_UNIT_CODED_SLICE_IDR_N_LP))
		{
			pcSlice->setCrossLayerBLAFlag(m_pcEncTop->getCrossLayerBLAFlag());
		}
		else
		{
			pcSlice->setCrossLayerBLAFlag(false);
		}

		// Set the nal unit type
		pcSlice->setNalUnitType(getNalUnitType(pocCurr, m_iLastIDR, isField));

#if NO_CLRAS_OUTPUT_FLAG
		if (m_layerId == 0 &&
			(pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_BLA_W_LP
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_BLA_W_RADL
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_BLA_N_LP
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_IDR_W_RADL
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_IDR_N_LP
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_CRA))
		{
			if (m_bFirst)
			{
				m_pcEncTop->setNoClrasOutputFlag(true);
			}
			else if (m_prevPicHasEos)
			{
				m_pcEncTop->setNoClrasOutputFlag(true);
			}
			else if (pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_BLA_W_LP
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_BLA_W_RADL
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_BLA_N_LP)
			{
				m_pcEncTop->setNoClrasOutputFlag(true);
			}
			else if (pcSlice->getCrossLayerBLAFlag() && (pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_IDR_W_RADL || pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_IDR_N_LP))
			{
				m_pcEncTop->setNoClrasOutputFlag(true);
			}
			else
			{
				m_pcEncTop->setNoClrasOutputFlag(false);
			}

			if (m_pcEncTop->getNoClrasOutputFlag())
			{
				for (UInt i = 0; i < m_pcCfg->getNumLayer(); i++)
				{
					m_ppcTEncTop[i]->setLayerInitializedFlag(false);
					m_ppcTEncTop[i]->setFirstPicInLayerDecodedFlag(false);
				}
			}
		}
#endif
		xCheckLayerReset(pcSlice);
		xSetNoRaslOutputFlag(pcSlice);
		xSetLayerInitializedFlag(pcSlice);

		if (m_pcEncTop->getAdaptiveResolutionChange() > 0 && m_layerId > 0 && pocCurr > m_pcEncTop->getAdaptiveResolutionChange())
		{
			pcSlice->setActiveNumILRRefIdx(0);
			pcSlice->setInterLayerPredEnabledFlag(false);
			pcSlice->setMFMEnabledFlag(false);
		}
#endif //SVC_EXTENSION

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

#if SVC_EXTENSION
		if (m_layerId > 0)
		{
			if (pcSlice->getSliceIdx() == 0)
			{
				// create buffers for scaling factors
				if (pcSlice->getNumILRRefIdx())
				{
					pcSlice->getPic()->createMvScalingFactor(pcSlice->getNumILRRefIdx());
					pcSlice->getPic()->createPosScalingFactor(pcSlice->getNumILRRefIdx());
				}
			}

			Int interLayerPredLayerIdcTmp[MAX_VPS_LAYER_IDX_PLUS1];
			Int activeNumILRRefIdxTmp = 0;

			for (Int i = 0; i < pcSlice->getActiveNumILRRefIdx(); i++)
			{
				UInt refLayerIdc = pcSlice->getInterLayerPredLayerIdc(i);
				UInt refLayerId = pcSlice->getVPS()->getRefLayerId(m_layerId, refLayerIdc);
				TComList<TComPic*> *cListPic = m_ppcTEncTop[pcSlice->getVPS()->getLayerIdxInVps(m_layerId)]->getRefLayerEnc(refLayerIdc)->getListPic();

				pcSlice->setBaseColPic(*cListPic, refLayerIdc);

				// Apply temporal layer restriction to inter-layer prediction
				Int maxTidIlRefPicsPlus1 = m_pcEncTop->getVPS()->getMaxTidIlRefPicsPlus1(pcSlice->getBaseColPic(refLayerIdc)->getSlice(0)->getLayerIdx(), pcSlice->getLayerIdx());
				if (((Int)(pcSlice->getBaseColPic(refLayerIdc)->getSlice(0)->getTLayer()) < maxTidIlRefPicsPlus1) || (maxTidIlRefPicsPlus1 == 0 && pcSlice->getBaseColPic(refLayerIdc)->getSlice(0)->getRapPicFlag()))
				{
					interLayerPredLayerIdcTmp[activeNumILRRefIdxTmp++] = refLayerIdc; // add picture to the list of valid inter-layer pictures
				}
				else
				{
					continue; // SHM: ILP is not valid due to temporal layer restriction
				}

				const Window &scalEL = pcSlice->getPPS()->getScaledRefLayerWindowForLayer(refLayerId);
				const Window &windowRL = pcSlice->getPPS()->getRefLayerWindowForLayer(pcSlice->getVPS()->getRefLayerId(m_layerId, refLayerIdc));
				Int widthBL = pcSlice->getBaseColPic(refLayerIdc)->getPicYuvRec()->getWidth(COMPONENT_Y) - windowRL.getWindowLeftOffset() - windowRL.getWindowRightOffset();
				Int heightBL = pcSlice->getBaseColPic(refLayerIdc)->getPicYuvRec()->getHeight(COMPONENT_Y) - windowRL.getWindowTopOffset() - windowRL.getWindowBottomOffset();
				Int widthEL = pcPic->getPicYuvRec()->getWidth(COMPONENT_Y) - scalEL.getWindowLeftOffset() - scalEL.getWindowRightOffset();
				Int heightEL = pcPic->getPicYuvRec()->getHeight(COMPONENT_Y) - scalEL.getWindowTopOffset() - scalEL.getWindowBottomOffset();

				// conformance check: the values of RefLayerRegionWidthInSamplesY, RefLayerRegionHeightInSamplesY, ScaledRefRegionWidthInSamplesY and ScaledRefRegionHeightInSamplesY shall be greater than 0
				assert(widthEL > 0 && heightEL > 0 && widthBL > 0 && heightBL > 0);

				// conformance check: ScaledRefRegionWidthInSamplesY shall be greater or equal to RefLayerRegionWidthInSamplesY and ScaledRefRegionHeightInSamplesY shall be greater or equal to RefLayerRegionHeightInSamplesY
				assert(widthEL >= widthBL && heightEL >= heightBL);

				// conformance check: when ScaledRefRegionWidthInSamplesY is equal to RefLayerRegionWidthInSamplesY, PhaseHorY shall be equal to 0, when ScaledRefRegionWidthInSamplesC is equal to RefLayerRegionWidthInSamplesC, PhaseHorC shall be equal to 0, when ScaledRefRegionHeightInSamplesY is equal to RefLayerRegionHeightInSamplesY, PhaseVerY shall be equal to 0, and when ScaledRefRegionHeightInSamplesC is equal to RefLayerRegionHeightInSamplesC, PhaseVerC shall be equal to 0.
				const ResamplingPhase &resamplingPhase = pcSlice->getPPS()->getResamplingPhase(refLayerId);

				assert(((widthEL != widthBL) || (resamplingPhase.phaseHorLuma == 0 && resamplingPhase.phaseHorChroma == 0))
					&& ((heightEL != heightBL) || (resamplingPhase.phaseVerLuma == 0 && resamplingPhase.phaseVerChroma == 0)));

				pcSlice->getPic()->setMvScalingFactor(refLayerIdc,
					widthEL == widthBL ? MV_SCALING_FACTOR_1X : Clip3(-4096, 4095, ((widthEL << 8) + (widthBL >> 1)) / widthBL),
					heightEL == heightBL ? MV_SCALING_FACTOR_1X : Clip3(-4096, 4095, ((heightEL << 8) + (heightBL >> 1)) / heightBL));

				pcSlice->getPic()->setPosScalingFactor(refLayerIdc, ((Int64(widthBL) << 16) + (widthEL >> 1)) / widthEL, ((Int64(heightBL) << 16) + (heightEL >> 1)) / heightEL);

				TComPicYuv* pBaseColRec = pcSlice->getBaseColPic(refLayerIdc)->getPicYuvRec();

#if CGS_3D_ASYMLUT
				if (pcSlice->getPPS()->getCGSFlag())
				{
					// all reference layers are currently taken as CGS reference layers
					m_Enc3DAsymLUTPPS.addRefLayerId(pcSlice->getVPS()->getRefLayerId(m_layerId, refLayerIdc));
					m_Enc3DAsymLUTPicUpdate.addRefLayerId(pcSlice->getVPS()->getRefLayerId(m_layerId, refLayerIdc));

					if (pcSlice->getPic()->getPosScalingFactor(refLayerIdc, 0) < POS_SCALING_FACTOR_1X || pcSlice->getPic()->getPosScalingFactor(refLayerIdc, 1) < POS_SCALING_FACTOR_1X) //if(pcPic->requireResampling(refLayerIdc))
					{
						//downsampling
						xDownScalePic(pcPic->getPicYuvOrg(), pcSlice->getBaseColPic(refLayerIdc)->getPicYuvOrg(), pcSlice->getSPS()->getBitDepths(), pcPic->getPosScalingFactor(refLayerIdc, 0));

						m_Enc3DAsymLUTPPS.setDsOrigPic(pcSlice->getBaseColPic(refLayerIdc)->getPicYuvOrg());
						m_Enc3DAsymLUTPicUpdate.setDsOrigPic(pcSlice->getBaseColPic(refLayerIdc)->getPicYuvOrg());
					}
					else
					{
						m_Enc3DAsymLUTPPS.setDsOrigPic(pcPic->getPicYuvOrg());
						m_Enc3DAsymLUTPicUpdate.setDsOrigPic(pcPic->getPicYuvOrg());
					}

					Bool bSignalPPS = m_bSeqFirst;
					bSignalPPS |= m_pcCfg->getGOPSize() > 1 ? pocCurr % m_pcCfg->getIntraPeriod() == 0 : pocCurr % m_pcCfg->getFrameRate() == 0;
					xDetermine3DAsymLUT(pcSlice, pcPic, refLayerIdc, m_pcCfg, bSignalPPS);

					// update PPS in TEncTop and TComPicSym classes
					m_pcEncTop->getPPS()->setCGSOutputBitDepthY(m_Enc3DAsymLUTPPS.getOutputBitDepthY());
					m_pcEncTop->getPPS()->setCGSOutputBitDepthC(m_Enc3DAsymLUTPPS.getOutputBitDepthC());
					pcPic->getPicSym()->getPPSToUpdate()->setCGSOutputBitDepthY(m_Enc3DAsymLUTPPS.getOutputBitDepthY());
					pcPic->getPicSym()->getPPSToUpdate()->setCGSOutputBitDepthC(m_Enc3DAsymLUTPPS.getOutputBitDepthC());

					m_Enc3DAsymLUTPPS.colorMapping(pcSlice->getBaseColPic(refLayerIdc)->getPicYuvRec(), m_pColorMappedPic);
					pBaseColRec = m_pColorMappedPic;
				}
#endif

				if (pcPic->requireResampling(refLayerIdc))
				{
					// check for the sample prediction picture type
					if (pcSlice->getVPS()->isSamplePredictionType(pcSlice->getVPS()->getLayerIdxInVps(m_layerId), pcSlice->getVPS()->getLayerIdxInVps(refLayerId)))
					{
						m_pcPredSearch->upsampleBasePic(pcSlice, refLayerIdc, pcPic->getFullPelBaseRec(refLayerIdc), pBaseColRec, pcPic->getPicYuvRec(), pcSlice->getBaseColPic(refLayerIdc)->getSlice(0)->getSPS()->getBitDepth(CHANNEL_TYPE_LUMA), pcSlice->getBaseColPic(refLayerIdc)->getSlice(0)->getSPS()->getBitDepth(CHANNEL_TYPE_CHROMA));
					}
				}
				else
				{
#if CGS_3D_ASYMLUT 
					pcPic->setFullPelBaseRec(refLayerIdc, pBaseColRec);
#else
					pcPic->setFullPelBaseRec(refLayerIdc, pcSlice->getBaseColPic(refLayerIdc)->getPicYuvRec());
#endif
				}
				pcSlice->setFullPelBaseRec(refLayerIdc, pcPic->getFullPelBaseRec(refLayerIdc));
			}

			// Update the list of active inter-layer pictures
			for (Int i = 0; i < activeNumILRRefIdxTmp; i++)
			{
				pcSlice->setInterLayerPredLayerIdc(interLayerPredLayerIdcTmp[i], i);
			}

			pcSlice->setActiveNumILRRefIdx(activeNumILRRefIdxTmp);

			if (pcSlice->getActiveNumILRRefIdx() == 0)
			{
				// No valid inter-layer pictures -> disable inter-layer prediction
				pcSlice->setInterLayerPredEnabledFlag(false);
			}

			if (pocCurr % m_pcCfg->getIntraPeriod() == 0)
			{
				if (pcSlice->getVPS()->getCrossLayerIrapAlignFlag())
				{
					TComList<TComPic*> *cListPic = m_ppcTEncTop[pcSlice->getVPS()->getLayerIdxInVps(m_layerId)]->getRefLayerEnc(0)->getListPic();
					TComPic* picLayer0 = pcSlice->getRefPic(*cListPic, pcSlice->getPOC());
					if (picLayer0)
					{
						pcSlice->setNalUnitType(picLayer0->getSlice(0)->getNalUnitType());
					}
					else
					{
						pcSlice->setNalUnitType(NAL_UNIT_CODED_SLICE_CRA);
					}
				}
			}

			if (pcSlice->getNalUnitType() >= NAL_UNIT_CODED_SLICE_BLA_W_LP && pcSlice->getNalUnitType() <= NAL_UNIT_CODED_SLICE_CRA)
			{
				if (pcSlice->getActiveNumILRRefIdx() == 0 && m_pcEncTop->getNumDirectRefLayers() == 0)
				{
					pcSlice->setSliceType(I_SLICE);
				}
				else if (!m_pcEncTop->getElRapSliceTypeB() && pcSlice->getSliceType() == B_SLICE)
				{
					pcSlice->setSliceType(P_SLICE);
				}
			}
		}
#else
		// Set the nal unit type
		pcSlice->setNalUnitType(getNalUnitType(pocCurr, m_iLastIDR, isField));
#endif //#if SVC_EXTENSION

		if (pcSlice->getTemporalLayerNonReferenceFlag())
		{
			if (pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_TRAIL_R &&
#if SVC_EXTENSION
			(m_iGopSize != 1 || m_ppcTEncTop[pcSlice->getVPS()->getLayerIdxInVps(m_layerId)]->getIntraPeriod() > 1))
#else
				!(m_iGopSize == 1 && pcSlice->getSliceType() == I_SLICE))
#endif
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
#if SVC_POC
				m_associatedIRAPPOC = pcSlice->getPOC();
				m_associatedIrapPocBeforeReset = pocCurr;
#else
				m_associatedIRAPPOC = pocCurr;
#endif
			}
			pcSlice->setAssociatedIRAPType(m_associatedIRAPType);
			pcSlice->setAssociatedIRAPPOC(m_associatedIRAPPOC);
#if SVC_POC
			pcSlice->setAssociatedIrapPocBeforeReset(m_associatedIrapPocBeforeReset);
#endif
		}
		// Do decoding refresh marking if any
#if NO_CLRAS_OUTPUT_FLAG
		pcSlice->decodingRefreshMarking(m_pocCRA, m_bRefreshPending, rcListPic, m_pcCfg->getEfficientFieldIRAPEnabled(), m_pcEncTop->getNoClrasOutputFlag());
#else
		pcSlice->decodingRefreshMarking(m_pocCRA, m_bRefreshPending, rcListPic, m_pcCfg->getEfficientFieldIRAPEnabled());
#endif
#if SVC_POC
		// m_pocCRA may have been update here; update m_pocCraWithoutReset
		m_pocCraWithoutReset = m_pocCRA + m_pcEncTop->getPocAdjustmentValue();
#endif
		m_pcEncTop->selectReferencePictureSet(pcSlice, pocCurr, iGOPid);
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

#if ALIGNED_BUMPING
		pcSlice->checkLeadingPictureRestrictions(rcListPic, true);
#endif
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

#if SVC_EXTENSION
		if (m_layerId > 0 && pcSlice->getActiveNumILRRefIdx())
		{
			if (pocCurr > 0 && pcSlice->isRADL() && pcPic->getSlice(0)->getBaseColPic(pcPic->getSlice(0)->getInterLayerPredLayerIdc(0))->getSlice(0)->isRASL())
			{
				pcSlice->setActiveNumILRRefIdx(0);
				pcSlice->setInterLayerPredEnabledFlag(0);
			}

			if (pcSlice->getNalUnitType() >= NAL_UNIT_CODED_SLICE_BLA_W_LP && pcSlice->getNalUnitType() <= NAL_UNIT_CODED_SLICE_CRA)
			{
				pcSlice->setNumRefIdx(REF_PIC_LIST_0, pcSlice->getActiveNumILRRefIdx());
				pcSlice->setNumRefIdx(REF_PIC_LIST_1, pcSlice->getActiveNumILRRefIdx());
			}
			else
			{
				pcSlice->setNumRefIdx(REF_PIC_LIST_0, pcSlice->getNumRefIdx(REF_PIC_LIST_0) + pcSlice->getActiveNumILRRefIdx());
				pcSlice->setNumRefIdx(REF_PIC_LIST_1, pcSlice->getNumRefIdx(REF_PIC_LIST_1) + pcSlice->getActiveNumILRRefIdx());
			}

			// check for the reference pictures whether there is at least one either temporal picture or ILRP with sample prediction type
			if (pcSlice->getNumRefIdx(REF_PIC_LIST_0) - pcSlice->getActiveNumILRRefIdx() == 0 && pcSlice->getNumRefIdx(REF_PIC_LIST_1) - pcSlice->getActiveNumILRRefIdx() == 0)
			{
				Bool foundSamplePredPicture = false;

				for (Int i = 0; i < pcSlice->getActiveNumILRRefIdx(); i++)
				{
					if (pcSlice->getVPS()->isSamplePredictionType(pcSlice->getVPS()->getLayerIdxInVps(m_layerId), pcSlice->getInterLayerPredLayerIdc(i)))
					{
						foundSamplePredPicture = true;
						break;
					}
				}

				if (!foundSamplePredPicture)
				{
					pcSlice->setSliceType(I_SLICE);
					pcSlice->setInterLayerPredEnabledFlag(0);
					pcSlice->setActiveNumILRRefIdx(0);
				}
			}
		}

		if ((pcSlice->getTLayer() == 0 && pcSlice->getLayerId() > 0)    // only for enhancement layer and with temporal layer 0
			&& !(pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_RADL_N
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_RADL_R
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_RASL_N
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_RASL_R
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_IDR_W_RADL
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_IDR_N_LP
				|| pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_CRA
				)
			)
		{
			Bool isSTSA = true;
			Bool isIntra = false;

			for (Int i = 0; i < pcSlice->getLayerId(); i++)
			{
				TComList<TComPic *> *cListPic = m_ppcTEncTop[pcSlice->getVPS()->getLayerIdxInVps(i)]->getListPic();
				TComPic *lowerLayerPic = pcSlice->getRefPic(*cListPic, pcSlice->getPOC());
				if (lowerLayerPic && pcSlice->getVPS()->getDirectDependencyFlag(pcSlice->getLayerIdx(), i))
				{
					if (lowerLayerPic->getSlice(0)->getSliceType() == I_SLICE)
					{
						isIntra = true;
					}
				}
			}

			for (Int ii = iGOPid + 1; ii < m_pcCfg->getGOPSize() && isSTSA; ii++)
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
			if (isSTSA == true && isIntra == false)
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

		if (pcSlice->getSliceType() == B_SLICE)
		{
			pcSlice->setColFromL0Flag(1 - uiColDir);
		}

		//  Set reference list
		if (m_layerId == 0 || (m_layerId > 0 && pcSlice->getActiveNumILRRefIdx() == 0))
		{
			pcSlice->setRefPicList(rcListPic);
		}

		if (m_layerId > 0 && pcSlice->getActiveNumILRRefIdx())
		{
			pcSlice->setILRPic(m_pcEncTop->getIlpList());
#if VIEW_SCALABILITY 
			pcSlice->setRefPicListModificationSvc(m_pcEncTop->getIlpList());
#else
			pcSlice->setRefPicListModificationSvc();
#endif
			pcSlice->setRefPicList(rcListPic, false, m_pcEncTop->getIlpList());

			if (pcSlice->getMFMEnabledFlag())
			{
				Bool found = false;
				UInt ColFromL0Flag = pcSlice->getColFromL0Flag();
				UInt ColRefIdx = pcSlice->getColRefIdx();

				for (Int colIdx = 0; colIdx < pcSlice->getNumRefIdx(RefPicList(1 - ColFromL0Flag)); colIdx++)
				{
					RefPicList refList = RefPicList(1 - ColFromL0Flag);
					TComPic* refPic = pcSlice->getRefPic(refList, colIdx);

					// It is a requirement of bitstream conformance when the collocated picture, used for temporal motion vector prediction, is an inter-layer reference picture, 
					// VpsInterLayerMotionPredictionEnabled[ LayerIdxInVps[ currLayerId ] ][ LayerIdxInVps[ rLId ] ] shall be equal to 1, where rLId is set equal to nuh_layer_id of the inter-layer picture.
					if (refPic->isILR(m_layerId) && pcSlice->getVPS()->isMotionPredictionType(pcSlice->getVPS()->getLayerIdxInVps(m_layerId), refPic->getLayerIdx())
						&& pcSlice->getBaseColPic(*m_ppcTEncTop[refPic->getLayerIdx()]->getListPic())->checkSameRefInfo() == true)
					{
						ColRefIdx = colIdx;
						found = true;
						break;
					}
				}

				if (found == false)
				{
					ColFromL0Flag = 1 - ColFromL0Flag;
					for (Int colIdx = 0; colIdx < pcSlice->getNumRefIdx(RefPicList(1 - ColFromL0Flag)); colIdx++)
					{
						RefPicList refList = RefPicList(1 - ColFromL0Flag);
						TComPic* refPic = pcSlice->getRefPic(refList, colIdx);

						// It is a requirement of bitstream conformance when the collocated picture, used for temporal motion vector prediction, is an inter-layer reference picture, 
						// VpsInterLayerMotionPredictionEnabled[ LayerIdxInVps[ currLayerId ] ][ LayerIdxInVps[ rLId ] ] shall be equal to 1, where rLId is set equal to nuh_layer_id of the inter-layer picture.
						if (refPic->isILR(m_layerId) && pcSlice->getVPS()->isMotionPredictionType(pcSlice->getVPS()->getLayerIdxInVps(m_layerId), refPic->getLayerIdx())
							&& pcSlice->getBaseColPic(*m_ppcTEncTop[refPic->getLayerIdx()]->getListPic())->checkSameRefInfo() == true)
						{
							ColRefIdx = colIdx;
							found = true;
							break;
						}
					}
				}

				if (found == true)
				{
					pcSlice->setColFromL0Flag(ColFromL0Flag);
					pcSlice->setColRefIdx(ColRefIdx);
				}
			}
		}
#else //SVC_EXTENSION
		//  Set reference list
		pcSlice->setRefPicList(rcListPic);
#endif //#if SVC_EXTENSION

		//  Slice info. refinement
		if ((pcSlice->getSliceType() == B_SLICE) && (pcSlice->getNumRefIdx(REF_PIC_LIST_1) == 0))
		{
			pcSlice->setSliceType(P_SLICE);
		}
		pcSlice->setEncCABACTableIdx(m_pcSliceEncoder->getEncCABACTableIdx());

		if (pcSlice->getSliceType() == B_SLICE)
		{
#if !SVC_EXTENSION
			pcSlice->setColFromL0Flag(1 - uiColDir);
#endif
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
#if SVC_EXTENSION
			if (pcSlice->getIdrPicFlag())
			{
				pcSlice->setEnableTMVPFlag(0);
			}
			else
#endif
				pcSlice->setEnableTMVPFlag(1);
		}
		else
		{
			pcSlice->setEnableTMVPFlag(0);
		}

#if SVC_EXTENSION
		if (m_layerId > 0 && !pcSlice->isIntra())
		{
			Int colFromL0Flag = 1;
			Int colRefIdx = 0;

			// check whether collocated picture is valid
			if (pcSlice->getEnableTMVPFlag())
			{
				colFromL0Flag = pcSlice->getColFromL0Flag();
				colRefIdx = pcSlice->getColRefIdx();

				TComPic* refPic = pcSlice->getRefPic(RefPicList(1 - colFromL0Flag), colRefIdx);

				assert(refPic);

				// It is a requirement of bitstream conformance when the collocated picture, used for temporal motion vector prediction, is an inter-layer reference picture, 
				// VpsInterLayerMotionPredictionEnabled[ LayerIdxInVps[ currLayerId ] ][ LayerIdxInVps[ rLId ] ] shall be equal to 1, where rLId is set equal to nuh_layer_id of the inter-layer picture.
				if (refPic->isILR(m_layerId) && !pcSlice->getVPS()->isMotionPredictionType(pcSlice->getVPS()->getLayerIdxInVps(m_layerId), refPic->getLayerIdx()))
				{
					pcSlice->setEnableTMVPFlag(false);
					pcSlice->setMFMEnabledFlag(false);
					colRefIdx = 0;
				}
			}

			// remove motion only ILRP from the end of the colFromL0Flag reference picture list
			RefPicList refList = RefPicList(colFromL0Flag);
			Int numRefIdx = pcSlice->getNumRefIdx(refList);

			if (numRefIdx > 0)
			{
				for (Int refIdx = pcSlice->getNumRefIdx(refList) - 1; refIdx > 0; refIdx--)
				{
					TComPic* refPic = pcSlice->getRefPic(refList, refIdx);

					if (!refPic->isILR(m_layerId) || (refPic->isILR(m_layerId) && pcSlice->getVPS()->isSamplePredictionType(pcSlice->getVPS()->getLayerIdxInVps(m_layerId), refPic->getLayerIdx())))
					{
						break;
					}
					else
					{
						assert(numRefIdx > 1);
						numRefIdx--;
					}
				}

				pcSlice->setNumRefIdx(refList, numRefIdx);
			}

			// remove motion only ILRP from the end of the (1-colFromL0Flag) reference picture list up to colRefIdx
			refList = RefPicList(1 - colFromL0Flag);
			numRefIdx = pcSlice->getNumRefIdx(refList);

			if (numRefIdx > 0)
			{
				for (Int refIdx = pcSlice->getNumRefIdx(refList) - 1; refIdx > colRefIdx; refIdx--)
				{
					TComPic* refPic = pcSlice->getRefPic(refList, refIdx);

					if (!refPic->isILR(m_layerId) || (refPic->isILR(m_layerId) && pcSlice->getVPS()->isSamplePredictionType(pcSlice->getVPS()->getLayerIdxInVps(m_layerId), refPic->getLayerIdx())))
					{
						break;
					}
					else
					{
						assert(numRefIdx > 1);
						numRefIdx--;
					}
				}

				pcSlice->setNumRefIdx(refList, numRefIdx);
			}

			assert(pcSlice->getNumRefIdx(REF_PIC_LIST_0) > 0 && (pcSlice->isInterP() || (pcSlice->isInterB() && pcSlice->getNumRefIdx(REF_PIC_LIST_1) > 0)));
		}
#endif

		/////////////////////////////////////////////////////////////////////////////////////////////////// Compress a slice
		// set adaptive search range for non-intra-slices
		if (m_pcCfg->getUseASR() && pcSlice->getSliceType() != I_SLICE)
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

#if U0132_TARGET_BITS_SATURATION
			if (m_pcRateCtrl->getCpbSaturationEnabled() && frameLevel != 0)
			{
				Int estimatedCpbFullness = m_pcRateCtrl->getCpbState() + m_pcRateCtrl->getBufferingRate();

				// prevent overflow
				if (estimatedCpbFullness - estimatedBits > (Int)(m_pcRateCtrl->getCpbSize()*0.9f))
				{
					estimatedBits = estimatedCpbFullness - (Int)(m_pcRateCtrl->getCpbSize()*0.9f);
				}

				estimatedCpbFullness -= m_pcRateCtrl->getBufferingRate();
				// prevent underflow
#if V0078_ADAPTIVE_LOWER_BOUND
				if (estimatedCpbFullness - estimatedBits < m_pcRateCtrl->getRCPic()->getLowerBound())
				{
					estimatedBits = max(200, estimatedCpbFullness - m_pcRateCtrl->getRCPic()->getLowerBound());
				}
#else
				if (estimatedCpbFullness - estimatedBits < (Int)(m_pcRateCtrl->getCpbSize()*0.1f))
				{
					estimatedBits = max(200, estimatedCpbFullness - (Int)(m_pcRateCtrl->getCpbSize()*0.1f));
				}
#endif

				m_pcRateCtrl->getRCPic()->setTargetBits(estimatedBits);
			}
#endif

			Int sliceQP = m_pcCfg->getInitialQP();
#if SVC_EXTENSION
			if ((pocCurr == 0 && m_pcCfg->getInitialQP() > 0) || (frameLevel == 0 && m_pcCfg->getForceIntraQP())) // QP is specified
#else
			if ((pcSlice->getPOC() == 0 && m_pcCfg->getInitialQP() > 0) || (frameLevel == 0 && m_pcCfg->getForceIntraQP())) // QP is specified
#endif
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

#if U0132_TARGET_BITS_SATURATION
					if (m_pcRateCtrl->getCpbSaturationEnabled())
					{
						Int estimatedCpbFullness = m_pcRateCtrl->getCpbState() + m_pcRateCtrl->getBufferingRate();

						// prevent overflow
						if (estimatedCpbFullness - bits > (Int)(m_pcRateCtrl->getCpbSize()*0.9f))
						{
							bits = estimatedCpbFullness - (Int)(m_pcRateCtrl->getCpbSize()*0.9f);
						}

						estimatedCpbFullness -= m_pcRateCtrl->getBufferingRate();
						// prevent underflow
#if V0078_ADAPTIVE_LOWER_BOUND
						if (estimatedCpbFullness - bits < m_pcRateCtrl->getRCPic()->getLowerBound())
						{
							bits = estimatedCpbFullness - m_pcRateCtrl->getRCPic()->getLowerBound();
						}
#else
						if (estimatedCpbFullness - bits < (Int)(m_pcRateCtrl->getCpbSize()*0.1f))
						{
							bits = estimatedCpbFullness - (Int)(m_pcRateCtrl->getCpbSize()*0.1f);
						}
#endif
					}
#endif

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

#if AVC_BASE
		if (m_layerId == 0 && m_pcEncTop->getVPS()->getNonHEVCBaseLayerFlag())
		{
			pcPic->getPicYuvOrg()->copyToPic(pcPic->getPicYuvRec());

			// Calculate for the base layer to be used in EL as Inter layer reference
			if (m_pcEncTop->getInterLayerWeightedPredFlag())
			{
				m_pcSliceEncoder->estimateILWpParam(pcSlice);
			}

			return;
		}
#endif

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
				m_pcSliceEncoder->precompressSlice(pcPic);
				m_pcSliceEncoder->compressSlice(pcPic, false, false);

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

#if N0383_IL_CONSTRAINED_TILE_SETS_SEI
		if (m_pcCfg->getInterLayerConstrainedTileSetsSEIEnabled())
		{
			xBuildTileSetsMap(pcPic->getPicSym());
		}
#endif

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
#if W0038_DB_OPT
			if (m_pcCfg->getDeblockingFilterMetric() == 2)
			{
				applyDeblockingFilterParameterSelection(pcPic, uiNumSliceSegments, iGOPid);
			}
			else
			{
#endif
				applyDeblockingFilterMetric(pcPic, uiNumSliceSegments);
#if W0038_DB_OPT
			}
#endif
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
#if SVC_EXTENSION && CGS_3D_ASYMLUT
		else if (m_pcCfg->getCGSFlag() && pcSlice->getLayerId() && pcSlice->getCGSOverWritePPS())
		{
			OutputNALUnit nalu(NAL_UNIT_PPS, 0, m_layerId);
			m_pcEntropyCoder->setBitstream(&nalu.m_Bitstream);
			m_pcEntropyCoder->encodePPS(pcSlice->getPPS(), &m_Enc3DAsymLUTPPS);
			accessUnit.push_back(new NALUnitEBSP(nalu));
		}
#endif

		if (m_pcCfg->getAccessUnitDelimiter())
		{
			xWriteAccessUnitDelimiter(accessUnit, pcSlice);
		}

		// reset presence of BP SEI indication
		m_bufferingPeriodSEIPresentInAU = false;
		// create prefix SEI associated with a picture
		xCreatePerPictureSEIMessages(iGOPid, leadingSeiMessages, nestedSeiMessages, pcSlice);

		/* use the main bitstream buffer for storing the marshalled picture */
		m_pcEntropyCoder->setBitstream(NULL);

		pcSlice = pcPic->getSlice(0);


#if HIGHER_LAYER_IRAP_SKIP_FLAG
		if (pcSlice->getSPS()->getUseSAO() && !(m_pcEncTop->getSkipPictureAtArcSwitch() && m_pcEncTop->getAdaptiveResolutionChange() > 0 && pcSlice->getLayerId() == 1 && pcSlice->getPOC() == m_pcEncTop->getAdaptiveResolutionChange()))
#else
		if (pcSlice->getSPS()->getUseSAO())
#endif
		{
			Bool sliceEnabled[MAX_NUM_COMPONENT];
			TComBitCounter tempBitCounter;
			tempBitCounter.resetBits();
			m_pcEncTop->getRDGoOnSbacCoder()->setBitstream(&tempBitCounter);
			m_pcSAO->initRDOCabacCoder(m_pcEncTop->getRDGoOnSbacCoder(), pcSlice);
#if OPTIONAL_RESET_SAO_ENCODING_AFTER_IRAP
			m_pcSAO->SAOProcess(pcPic, sliceEnabled, pcPic->getSlice(0)->getLambdas(),
				m_pcCfg->getTestSAODisableAtPictureLevel(),
				m_pcCfg->getSaoEncodingRate(),
				m_pcCfg->getSaoEncodingRateChroma(),
				m_pcCfg->getSaoCtuBoundary(),
				m_pcCfg->getSaoResetEncoderStateAfterIRAP());
#else
			m_pcSAO->SAOProcess(pcPic, sliceEnabled, pcPic->getSlice(0)->getLambdas(), m_pcCfg->getTestSAODisableAtPictureLevel(), m_pcCfg->getSaoEncodingRate(), m_pcCfg->getSaoEncodingRateChroma(), m_pcCfg->getSaoCtuBoundary());
#endif
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
#if SVC_EXTENSION
			OutputNALUnit nalu(pcSlice->getNalUnitType(), pcSlice->getTLayer(), m_layerId);
#else
			OutputNALUnit nalu(pcSlice->getNalUnitType(), pcSlice->getTLayer());
#endif
			m_pcEntropyCoder->setBitstream(&nalu.m_Bitstream);

#if SVC_EXTENSION
			if (pcSlice->isIRAP())
			{
				//the inference for NoOutputPriorPicsFlag
				// KJS: This cannot happen at the encoder
				if (!m_bFirst && pcSlice->isIRAP() && m_noRaslOutputFlag)
				{
					if (pcSlice->getNalUnitType() == NAL_UNIT_CODED_SLICE_CRA)
					{
						pcSlice->setNoOutputPriorPicsFlag(true);
					}
				}
			}
#else
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
#endif

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
#if SVC_EXTENSION
				tmpBitsBeforeWriting = m_pcEntropyCoder->getNumberOfWrittenBits();
				m_pcEntropyCoder->encodeTilesWPPEntryPoint(pcSlice);
				actualHeadBits += (m_pcEntropyCoder->getNumberOfWrittenBits() - tmpBitsBeforeWriting);
				m_pcEntropyCoder->encodeSliceHeaderExtn(pcSlice, actualHeadBits);
#else
				m_pcEntropyCoder->encodeTilesWPPEntryPoint(pcSlice);
#endif

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
		if (m_pcCfg->getDecodedPictureHashSEIType() != HASHTYPE_NONE)
		{
			SEIDecodedPictureHash *decodedPictureHashSei = new SEIDecodedPictureHash();
			m_seiEncoder.initDecodedPictureHashSEI(decodedPictureHashSei, pcPic, digestStr, pcSlice->getSPS()->getBitDepths());
			trailingSeiMessages.push_back(decodedPictureHashSei);
		}

		m_pcCfg->setEncodedFlag(iGOPid, true);

		Double PSNR_Y;
		xCalculateAddPSNRs(isField, isTff, iGOPid, pcPic, accessUnit, rcListPic, dEncTime, snr_conversion, printFrameMSE, &PSNR_Y);

		// Only produce the Green Metadata SEI message with the last picture.
		if (m_pcCfg->getSEIGreenMetadataInfoSEIEnable() && pcSlice->getPOC() == (m_pcCfg->getFramesToBeEncoded() - 1))
		{
			SEIGreenMetadataInfo *seiGreenMetadataInfo = new SEIGreenMetadataInfo;
			m_seiEncoder.initSEIGreenMetadataInfo(seiGreenMetadataInfo, (UInt)(PSNR_Y * 100 + 0.5));
			trailingSeiMessages.push_back(seiGreenMetadataInfo);
		}

#if O0164_MULTI_LAYER_HRD
		xWriteTrailingSEIMessages(trailingSeiMessages, accessUnit, pcSlice->getTLayer(), pcSlice->getVPS(), pcSlice->getSPS());
#else
		xWriteTrailingSEIMessages(trailingSeiMessages, accessUnit, pcSlice->getTLayer(), pcSlice->getSPS());
#endif

		printHash(m_pcCfg->getDecodedPictureHashSEIType(), digestStr);

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
#if U0132_TARGET_BITS_SATURATION
			if (m_pcRateCtrl->getCpbSaturationEnabled())
			{
				m_pcRateCtrl->updateCpbState(actualTotalBits);
				printf(" [CPB %6d bits]", m_pcRateCtrl->getCpbState());
			}
#endif
		}

		xCreatePictureTimingSEI(m_pcCfg->getEfficientFieldIRAPEnabled() ? effFieldIRAPMap.GetIRAPGOPid() : 0, leadingSeiMessages, nestedSeiMessages, duInfoSeiMessages, pcSlice, isField, duData);
		if (m_pcCfg->getScalableNestingSEIEnabled())
		{
			xCreateScalableNestingSEI(leadingSeiMessages, nestedSeiMessages);
		}
#if O0164_MULTI_LAYER_HRD
		xWriteLeadingSEIMessages(leadingSeiMessages, duInfoSeiMessages, accessUnit, pcSlice->getTLayer(), pcSlice->getVPS(), pcSlice->getSPS(), duData);
		xWriteDuSEIMessages(duInfoSeiMessages, accessUnit, pcSlice->getTLayer(), pcSlice->getVPS(), pcSlice->getSPS(), duData);
#else
		xWriteLeadingSEIMessages(leadingSeiMessages, duInfoSeiMessages, accessUnit, pcSlice->getTLayer(), pcSlice->getSPS(), duData);
		xWriteDuSEIMessages(duInfoSeiMessages, accessUnit, pcSlice->getTLayer(), pcSlice->getSPS(), duData);
#endif

#if SVC_EXTENSION
		m_prevPicHasEos = false;
		if (m_pcCfg->getLayerSwitchOffBegin() < m_pcCfg->getLayerSwitchOffEnd())
		{
			Int pocNext;
			if (iGOPid == m_iGopSize - 1)
			{
				pocNext = iPOCLast - iNumPicRcvd + m_iGopSize + m_pcCfg->getGOPEntry(0).m_POC;
			}
			else
			{
				pocNext = iPOCLast - iNumPicRcvd + m_pcCfg->getGOPEntry(iGOPid + 1).m_POC;
			}

			if (pocNext > m_pcCfg->getLayerSwitchOffBegin() && pocCurr < m_pcCfg->getLayerSwitchOffEnd())
			{
				OutputNALUnit nalu(NAL_UNIT_EOS, 0, pcSlice->getLayerId());
				m_pcEntropyCoder->setEntropyCoder(m_pcCavlcCoder);
				accessUnit.push_back(new NALUnitEBSP(nalu));
				m_prevPicHasEos = true;
			}
		}
#endif

		pcPic->getPicYuvRec()->copyToPic(pcPicYuvRecOut);

#if SVC_EXTENSION
		pcPicYuvRecOut->setReconstructed(true);
		m_pcEncTop->setFirstPicInLayerDecodedFlag(true);
#endif

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
#if REDUCED_ENCODER_MEMORY
#if !SVC_EXTENSION // syntax data is needed when picture is used as a base layer
		pcPic->releaseReconstructionIntermediateData();
		if (!isField) // don't release the source data for field-coding because the fields are dealt with in pairs. // TODO: release source data for interlace simulations.
		{
			pcPic->releaseEncoderSourceImageData();
		}
#endif
#endif
	} // iGOPid-loop

	delete pcBitstreamRedirect;

#if SVC_EXTENSION
	assert(m_iNumPicCoded <= 1);
#else
	assert((m_iNumPicCoded == iNumPicRcvd));
#endif
}