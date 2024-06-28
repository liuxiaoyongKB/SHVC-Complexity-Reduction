Void TEncSlice::precompressSlice(TComPic* pcPic)
{
	// if deltaQP RD is not used, simply return
	if (m_pcCfg->getDeltaQpRD() == 0)
	{
		return;
	}

	if (m_pcCfg->getUseRateCtrl())
	{
		printf("\nMultiple QP optimization is not allowed when rate control is enabled.");
		assert(0);
		return;
	}

	TComSlice* pcSlice = pcPic->getSlice(getSliceIdx());

	if (pcSlice->getDependentSliceSegmentFlag())
	{
		// if this is a dependent slice segment, then it was optimised
		// when analysing the entire slice.
		return;
	}

	if (pcSlice->getSliceMode() == FIXED_NUMBER_OF_BYTES)
	{
		// TODO: investigate use of average cost per CTU so that this Slice Mode can be used.
		printf("\nUnable to optimise Slice-level QP if Slice Mode is set to FIXED_NUMBER_OF_BYTES\n");
		assert(0);
		return;
	}

	Double     dPicRdCostBest = MAX_DOUBLE;
	UInt       uiQpIdxBest = 0;

	Double dFrameLambda;
#if FULL_NBIT
	Int    SHIFT_QP = 12 + 6 * (pcSlice->getSPS()->getBitDepth(CHANNEL_TYPE_LUMA) - 8);
#else
	Int    SHIFT_QP = 12;
#endif

	// set frame lambda
	if (m_pcCfg->getGOPSize() > 1)
	{
		dFrameLambda = 0.68 * pow(2, (m_viRdPicQp[0] - SHIFT_QP) / 3.0) * (pcSlice->isInterB() ? 2 : 1);
	}
	else
	{
		dFrameLambda = 0.68 * pow(2, (m_viRdPicQp[0] - SHIFT_QP) / 3.0);
	}
	m_pcRdCost->setFrameLambda(dFrameLambda);

	// for each QP candidate
	for (UInt uiQpIdx = 0; uiQpIdx < 2 * m_pcCfg->getDeltaQpRD() + 1; uiQpIdx++)
	{
		pcSlice->setSliceQp(m_viRdPicQp[uiQpIdx]);
#if ADAPTIVE_QP_SELECTION
		pcSlice->setSliceQpBase(m_viRdPicQp[uiQpIdx]);
#endif
		setUpLambda(pcSlice, m_vdRdPicLambda[uiQpIdx], m_viRdPicQp[uiQpIdx]);

		// try compress
		compressSlice(pcPic, true, m_pcCfg->getFastDeltaQp());

		UInt64 uiPicDist = m_uiPicDist; // Distortion, as calculated by compressSlice.
		// NOTE: This distortion is the chroma-weighted SSE distortion for the slice.
		//       Previously a standard SSE distortion was calculated (for the entire frame).
		//       Which is correct?

		// TODO: Update loop filter, SAO and distortion calculation to work on one slice only.
		// m_pcGOPEncoder->preLoopFilterPicAll( pcPic, uiPicDist );

		// compute RD cost and choose the best
		Double dPicRdCost = m_pcRdCost->calcRdCost((Double)m_uiPicTotalBits, (Double)uiPicDist, DF_SSE_FRAME);

		if (dPicRdCost < dPicRdCostBest)
		{
			uiQpIdxBest = uiQpIdx;
			dPicRdCostBest = dPicRdCost;
		}
	}

	// set best values
	pcSlice->setSliceQp(m_viRdPicQp[uiQpIdxBest]);
#if ADAPTIVE_QP_SELECTION
	pcSlice->setSliceQpBase(m_viRdPicQp[uiQpIdxBest]);
#endif
	setUpLambda(pcSlice, m_vdRdPicLambda[uiQpIdxBest], m_viRdPicQp[uiQpIdxBest]);
}