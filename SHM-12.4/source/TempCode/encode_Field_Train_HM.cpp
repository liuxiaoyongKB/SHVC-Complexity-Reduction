Void TEncTop::encode_train(Double *Feature0, Double *Feature1, Double *Feature2,
	Int *Truth0, Int *Truth1, Int *Truth2,
	Int frameSize0, Int frameSize1, Int frameSize2, Int feature_num_level0, Int feature_num_level1, Int feature_num_level2,
	Bool flush, TComPicYuv* pcPicYuvOrg, TComPicYuv* pcPicYuvTrueOrg, const InputColourSpaceConversion snrCSC, TComList<TComPicYuv*>& rcListPicYuvRecOut, std::list<AccessUnit>& accessUnitsOut, Int& iNumEncoded, Bool isTff)
{
	iNumEncoded = 0;

	for (Int fieldNum = 0; fieldNum < 2; fieldNum++)
	{
		if (pcPicYuvOrg)
		{

			/* -- field initialization -- */
			const Bool isTopField = isTff == (fieldNum == 0);

			TComPic *pcField;
			xGetNewPicBuffer(pcField);
			pcField->setReconMark(false);                     // where is this normally?

			if (fieldNum == 1)                                   // where is this normally?
			{
				TComPicYuv* rpcPicYuvRec;

				// org. buffer
				if (rcListPicYuvRecOut.size() >= (UInt)m_iGOPSize + 1) // need to maintain field 0 in list of RecOuts while processing field 1. Hence +1 on m_iGOPSize.
				{
					rpcPicYuvRec = rcListPicYuvRecOut.popFront();
				}
				else
				{
					rpcPicYuvRec = new TComPicYuv;
					rpcPicYuvRec->create(m_iSourceWidth, m_iSourceHeight, m_chromaFormatIDC, m_maxCUWidth, m_maxCUHeight, m_maxTotalCUDepth, true);
				}
				rcListPicYuvRecOut.pushBack(rpcPicYuvRec);
			}

			pcField->getSlice(0)->setPOC(m_iPOCLast);        // superfluous?
			pcField->getPicYuvRec()->setBorderExtension(false);// where is this normally?

			pcField->setTopField(isTopField);                  // interlaced requirement

			for (UInt componentIndex = 0; componentIndex < pcPicYuvOrg->getNumberValidComponents(); componentIndex++)
			{
				const ComponentID component = ComponentID(componentIndex);
				const UInt stride = pcPicYuvOrg->getStride(component);

				separateFields((pcPicYuvOrg->getBuf(component) + pcPicYuvOrg->getMarginX(component) + (pcPicYuvOrg->getMarginY(component) * stride)),
					pcField->getPicYuvOrg()->getAddr(component),
					pcPicYuvOrg->getStride(component),
					pcPicYuvOrg->getWidth(component),
					pcPicYuvOrg->getHeight(component),
					isTopField);

				separateFields((pcPicYuvTrueOrg->getBuf(component) + pcPicYuvTrueOrg->getMarginX(component) + (pcPicYuvTrueOrg->getMarginY(component) * stride)),
					pcField->getPicYuvTrueOrg()->getAddr(component),
					pcPicYuvTrueOrg->getStride(component),
					pcPicYuvTrueOrg->getWidth(component),
					pcPicYuvTrueOrg->getHeight(component),
					isTopField);
			}

			// compute image characteristics
			if (getUseAdaptiveQP())
			{
				m_cPreanalyzer.xPreanalyze(dynamic_cast<TEncPic*>(pcField));
			}
		}

		if (m_iNumPicRcvd && ((flush&&fieldNum == 1) || (m_iPOCLast / 2) == 0 || m_iNumPicRcvd == m_iGOPSize))
		{
			// compress GOP
			m_cGOPEncoder.compressGOP_train(Feature0, Feature1, Feature2,
				Truth0, Truth1, Truth2,
				frameSize0, frameSize1, frameSize2, feature_num_level0, feature_num_level1, feature_num_level2,
				m_iPOCLast, m_iNumPicRcvd, m_cListPic, rcListPicYuvRecOut, accessUnitsOut, true, isTff, snrCSC, m_printFrameMSE);

			iNumEncoded += m_iNumPicRcvd;
			m_uiNumAllPicCoded += m_iNumPicRcvd;
			m_iNumPicRcvd = 0;
		}
	}
}