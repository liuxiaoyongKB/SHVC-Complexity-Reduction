Void TEncTop::encode_train(Double *Feature0, Double *Feature1, Double *Feature2,
	Int *Truth0, Int *Truth1, Int *Truth2,
	Int frameSize0, Int frameSize1, Int frameSize2, Int feature_num_level0, Int feature_num_level1, Int feature_num_level2,
	Bool flush, TComPicYuv* pcPicYuvOrg, TComPicYuv* pcPicYuvTrueOrg, const InputColourSpaceConversion snrCSC, TComList<TComPicYuv*>& rcListPicYuvRecOut, std::list<AccessUnit>& accessUnitsOut, Int& iNumEncoded)
{
	if (pcPicYuvOrg != NULL)
	{
		// get original YUV
		TComPic* pcPicCurr = NULL;

		xGetNewPicBuffer(pcPicCurr);
		pcPicYuvOrg->copyToPic(pcPicCurr->getPicYuvOrg());
		pcPicYuvTrueOrg->copyToPic(pcPicCurr->getPicYuvTrueOrg());

		// compute image characteristics
		if (getUseAdaptiveQP())
		{
			m_cPreanalyzer.xPreanalyze(dynamic_cast<TEncPic*>(pcPicCurr));
		}
	}

	if ((m_iNumPicRcvd == 0) || (!flush && (m_iPOCLast != 0) && (m_iNumPicRcvd != m_iGOPSize) && (m_iGOPSize != 0)))
	{
		iNumEncoded = 0;
		return;
	}

	if (m_RCEnableRateControl)
	{
		m_cRateCtrl.initRCGOP(m_iNumPicRcvd);
	}

	// compress GOP
	m_cGOPEncoder.compressGOP_train(Feature0, Feature1, Feature2,
		Truth0, Truth1, Truth2,
		frameSize0, frameSize1, frameSize2, feature_num_level0, feature_num_level1, feature_num_level2,
		m_iPOCLast, m_iNumPicRcvd, m_cListPic, rcListPicYuvRecOut, accessUnitsOut, false, false, snrCSC, m_printFrameMSE);

	if (m_RCEnableRateControl)
	{
		m_cRateCtrl.destroyRCGOP();
	}

	iNumEncoded = m_iNumPicRcvd;
	m_iNumPicRcvd = 0;
	m_uiNumAllPicCoded += iNumEncoded;
}
