Void TEncTop::encode(Bool flush, TComPicYuv* pcPicYuvOrg, TComPicYuv* pcPicYuvTrueOrg, const InputColourSpaceConversion snrCSC, TComList<TComPicYuv*>& rcListPicYuvRecOut, std::list<AccessUnit>& accessUnitsOut, Int& iNumEncoded)
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
	m_cGOPEncoder.compressGOP(m_iPOCLast, m_iNumPicRcvd, m_cListPic, rcListPicYuvRecOut, accessUnitsOut, false, false, snrCSC, m_printFrameMSE);

	if (m_RCEnableRateControl)
	{
		m_cRateCtrl.destroyRCGOP();
	}

	iNumEncoded = m_iNumPicRcvd;
	m_iNumPicRcvd = 0;
	m_uiNumAllPicCoded += iNumEncoded;
}