/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2016, ITU/ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

 /** \file     TAppEncTop.cpp
	 \brief    Encoder application class
 */

#include <list>
#include <fstream>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <assert.h>
#if !SVC_EXTENSION
#include <iomanip>
#endif

#include "TAppEncTop.h"
#include "TLibEncoder/AnnexBwrite.h"
#if Machine_Learning_Debug
#include "TLibCommon/svm.h"
#endif

using namespace std;

#if Machine_Learning_Debug
// #include <opencv2/opencv.hpp>
// #include <opencv2/highgui//highgui.hpp>
// #include <opencv2/ml/ml.hpp>
// using namespace cv;

#define Malloc(type,n) (type *)malloc((n)*sizeof(type))
#endif

//! \ingroup TAppEncoder
//! \{

#if Machine_Learning_Debug
ofstream f0("data0.txt"/*,ios::app*/);
ofstream f1("data1.txt"/*,ios::app*/);
ofstream f2("data2.txt"/*,ios::app*/);
//ofstream f3("data3.txt");
#endif

// ====================================================================================================================
// Constructor / destructor / initialization / destroy
// ====================================================================================================================

TAppEncTop::TAppEncTop()
{
	m_iFrameRcvd = 0;
	m_totalBytes = 0;
	m_essentialBytes = 0;
#if SVC_EXTENSION
	memset(m_apcTVideoIOYuvInputFile, 0, sizeof(m_apcTVideoIOYuvInputFile));
	memset(m_apcTVideoIOYuvReconFile, 0, sizeof(m_apcTVideoIOYuvReconFile));
	memset(m_apcTEncTop, 0, sizeof(m_apcTEncTop));
#endif
}

TAppEncTop::~TAppEncTop()
{
}

#if Machine_Learning_Debug
Void TAppEncTop::SVM_Train_Online(Double *Feature0, Double *Feature1, Double *Feature2,
	Int *Truth0, Int *Truth1, Int *Truth2,
#if PUMode_FastDecision
	Int *Truth0_PU, Int *Truth1_PU, Int *Truth2_PU,
#endif
	maxmin *M0, maxmin *M1, maxmin *M2, Int Feature_Num_Level0, Int Feature_Num_Level1, Int Feature_Num_Level2, Int trainingnum,
	Int frameSize0, Int frameSize1, Int frameSize2,
	Int W0, Int W1, Int W2, Int WIDTH, Int HEIGHT,
	svm_model *&model0, svm_model*&model1, svm_model*&model2
#if PUMode_FastDecision
	, svm_model *&model0_PU, svm_model*&model1_PU, svm_model*&model2_PU
#endif
)
{
	struct svm_parameter param;		// set by parse_command_line
	struct svm_problem prob0;		// set by read_problem
	struct svm_problem prob1;		// set by read_problem
	struct svm_problem prob2;		// set by read_problem
	struct svm_node *data0, *data1, *data2;

#if PUMode_FastDecision
	// PU模式决策模型
	struct svm_problem prob0_PU;		// set by read_problem
	struct svm_problem prob1_PU;		// set by read_problem
	struct svm_problem prob2_PU;		// set by read_problem
	struct svm_node *data0_PU, *data1_PU, *data2_PU;
#endif

	param.svm_type = C_SVC;
	param.C = 1;
	param.degree = 3;
	param.kernel_type = RBF;
	param.eps = 1e-5;
	param.gamma = 0.5;

	param.coef0 = 0;
	param.nu = 0.5;
	param.cache_size = 100;
	param.p = 0.1;
	param.shrinking = 1;
	param.probability = 0;
	param.nr_weight = 2;
	param.weight_label = new Int[param.nr_weight];
	param.weight_label[0] = -1;
	param.weight_label[1] = +1;
	param.weight = new Double[param.nr_weight];
	param.weight[0] = 1;
	param.weight[1] = 1;//	LXY
	//--------------------------------------------------------------------------
	for (Int j = 0; j < Feature_Num_Level0; j++)
	{
		M0[j].maxvalue = Feature0[j];
		M0[j].minvalue = Feature0[j];
	}
	for (Int j = 0; j < Feature_Num_Level1; j++)
	{
		M1[j].maxvalue = Feature1[j];
		M1[j].minvalue = Feature1[j];
	}
	for (Int j = 0; j < Feature_Num_Level2; j++)
	{
		M2[j].maxvalue = Feature2[j];
		M2[j].minvalue = Feature2[j];
	}


	int W00 = WIDTH / 64, H00 = HEIGHT / 64;
	int W11 = WIDTH / 32, H11 = HEIGHT / 32;
	int W22 = WIDTH / 16, H22 = HEIGHT / 16;

	for (int i = 0; i < trainingnum; i++)
	{
		for (int j = 0; j < frameSize0; j++)
		{
			if (0 != Truth0[i*frameSize0 + j] && (j / W0 < H00&&j%W0 < W00))
			{
				for (int k = 0; k < Feature_Num_Level0; k++)
				{
					if (Feature0[(i*frameSize0 + j)*Feature_Num_Level0 + k] >= M0[k].maxvalue)
						M0[k].maxvalue = Feature0[(i*frameSize0 + j)*Feature_Num_Level0 + k];
					else if (Feature0[(i*frameSize0 + j)*Feature_Num_Level0 + k] <= M0[k].minvalue)
						M0[k].minvalue = Feature0[(i*frameSize0 + j)*Feature_Num_Level0 + k];
				}
			}
		}
	}

	for (int frame = 0; frame < trainingnum; frame++)
	{
		for (int i = 0; i < frameSize0; i++)
		{
			int tempx = 2 * (i / W0), tempy = 2 * (i%W0);
			for (int l = 0; l < 4; l++)
			{
				int temp1 = W1 * (tempx + l / 2) + (tempy + l % 2);
				if (0 != Truth1[4 * i + l + frame * frameSize1] && (temp1 / W1 < H11&&temp1%W1 < W11))
				{
					for (int j = 0; j < Feature_Num_Level1; j++)
					{
						if (Feature1[(frame*frameSize1 + temp1)*Feature_Num_Level1 + j] >= M1[j].maxvalue)     M1[j].maxvalue = Feature1[(frame*frameSize1 + temp1)*Feature_Num_Level1 + j];
						else if (Feature1[(frame*frameSize1 + temp1)*Feature_Num_Level1 + j] <= M1[j].minvalue) M1[j].minvalue = Feature1[(frame*frameSize1 + temp1)*Feature_Num_Level1 + j];
					}
				}
			}
		}
	}

	for (int frame = 0; frame < trainingnum; frame++)
	{
		for (int i = 0; i < frameSize0; i++)
		{
			int tempx = 2 * (i / W0), tempy = 2 * (i%W0);
			for (int l = 0; l < 4; l++)
			{
				int temp1 = W1 * (tempx + l / 2) + (tempy + l % 2);
				int tempxx = 2 * (temp1 / W1), tempyy = 2 * (temp1%W1);
				for (int m = 0; m < 4; m++)
				{
					int temp2 = W2 * (tempxx + m / 2) + (tempyy + m % 2);
					if (0 != Truth2[4 * (4 * i + l) + m + frame * frameSize2] && (temp2 / W2 < H22&&temp2%W2 < W22))
					{
						for (int j = 0; j < Feature_Num_Level2; j++)
						{
							if (Feature2[(frame*frameSize2 + temp2)*Feature_Num_Level2 + j] >= M2[j].maxvalue)     M2[j].maxvalue = Feature2[(frame*frameSize2 + temp2)*Feature_Num_Level2 + j];
							else if (Feature2[(frame*frameSize2 + temp2)*Feature_Num_Level2 + j] <= M2[j].minvalue) M2[j].minvalue = Feature2[(frame*frameSize2 + temp2)*Feature_Num_Level2 + j];
						}
					}
				}

			}
		}
	}
	//=========================================================================//
	double *level0_mean_positive = new double[Feature_Num_Level0]; memset(level0_mean_positive, 0, sizeof(double)*Feature_Num_Level0);
	double *level1_mean_positive = new double[Feature_Num_Level1]; memset(level1_mean_positive, 0, sizeof(double)*Feature_Num_Level1);
	double *level2_mean_positive = new double[Feature_Num_Level2]; memset(level2_mean_positive, 0, sizeof(double)*Feature_Num_Level2);

	double *level0_mean_negative = new double[Feature_Num_Level0]; memset(level0_mean_negative, 0, sizeof(double)*Feature_Num_Level0);
	double *level1_mean_negative = new double[Feature_Num_Level1]; memset(level1_mean_negative, 0, sizeof(double)*Feature_Num_Level1);
	double *level2_mean_negative = new double[Feature_Num_Level2]; memset(level2_mean_negative, 0, sizeof(double)*Feature_Num_Level2);

	int num_0_positive = 0, num_1_positive = 0, num_2_positive = 0;
	int num_0_negative = 0, num_1_negative = 0, num_2_negative = 0;

	for (int i = 0; i < trainingnum; i++)
	{
		for (int j = 0; j < frameSize0; j++)
		{
			if (+1 == Truth0[i*frameSize0 + j] && (j / W0 < H00&&j%W0 < W00))
			{
				for (int k = 0; k < Feature_Num_Level0; k++)
				{
					double temp = Feature0[(i*frameSize0 + j)*Feature_Num_Level0 + k];
					level0_mean_positive[k] += -1 + 2.0*(temp - M0[k].minvalue) / (M0[k].maxvalue - M0[k].minvalue + 1e-8);
				}
				num_0_positive++;
			}
			else if (-1 == Truth0[i*frameSize0 + j] && (j / W0 < H00&&j%W0 < W00))
			{
				for (int k = 0; k < Feature_Num_Level0; k++)
				{
					double temp = Feature0[(i*frameSize0 + j)*Feature_Num_Level0 + k];
					level0_mean_negative[k] += -1 + 2.0*(temp - M0[k].minvalue) / (M0[k].maxvalue - M0[k].minvalue + 1e-8);
				}
				num_0_negative++;
			}
		}
	}

	for (int frame = 0; frame < trainingnum; frame++)
	{
		for (int i = 0; i < frameSize0; i++)
		{
			int tempx = 2 * (i / W0), tempy = 2 * (i%W0);
			for (int l = 0; l < 4; l++)
			{
				int temp1 = W1 * (tempx + l / 2) + (tempy + l % 2);
				if (+1 == Truth1[4 * i + l + frame * frameSize1] && (temp1 / W1 < H11&&temp1%W1 < W11))
				{
					for (int j = 0; j < Feature_Num_Level1; j++)
					{
						double temp = Feature1[(frame*frameSize1 + temp1)*Feature_Num_Level1 + j];
						level1_mean_positive[j] += -1 + 2.0*(temp - M1[j].minvalue) / (M1[j].maxvalue - M1[j].minvalue + 1e-8);
					}
					num_1_positive++;
				}
				else if (-1 == Truth1[4 * i + l + frame * frameSize1] && (temp1 / W1 < H11&&temp1%W1 < W11))
				{
					for (int j = 0; j < Feature_Num_Level1; j++)
					{
						double temp = Feature1[(frame*frameSize1 + temp1)*Feature_Num_Level1 + j];
						level1_mean_negative[j] += -1 + 2.0*(temp - M1[j].minvalue) / (M1[j].maxvalue - M1[j].minvalue + 1e-8);
					}
					num_1_negative++;
				}
			}
		}
	}

	for (int frame = 0; frame < trainingnum; frame++)
	{
		for (int i = 0; i < frameSize0; i++)
		{
			int tempx = 2 * (i / W0), tempy = 2 * (i%W0);
			for (int l = 0; l < 4; l++)
			{
				int temp1 = W1 * (tempx + l / 2) + (tempy + l % 2);
				int tempxx = 2 * (temp1 / W1), tempyy = 2 * (temp1%W1);
				for (int m = 0; m < 4; m++)
				{
					int temp2 = W2 * (tempxx + m / 2) + (tempyy + m % 2);
					if (+1 == Truth2[4 * (4 * i + l) + m + frame * frameSize2] && (temp2 / W2 < H22&&temp2%W2 < W22))
					{
						for (int j = 0; j < Feature_Num_Level2; j++)
						{
							double temp = Feature2[(frame*frameSize2 + temp2)*Feature_Num_Level2 + j];
							level2_mean_positive[j] += -1 + 2.0*(temp - M2[j].minvalue) / (M2[j].maxvalue - M2[j].minvalue + 1e-8);
						}
						num_2_positive++;
					}
					else if (-1 == Truth2[4 * (4 * i + l) + m + frame * frameSize2] && (temp2 / W2 < H22&&temp2%W2 < W22))
					{
						for (int j = 0; j < Feature_Num_Level2; j++)
						{
							double temp = Feature2[(frame*frameSize2 + temp2)*Feature_Num_Level2 + j];
							level2_mean_negative[j] += -1 + 2.0*(temp - M2[j].minvalue) / (M2[j].maxvalue - M2[j].minvalue + 1e-8);
						}
						num_2_negative++;
					}
				}
			}
		}
	}

	for (int k = 0; k < Feature_Num_Level0; k++)
	{
		level0_mean_positive[k] /= 1.0*num_0_positive + 1e-8;
		level0_mean_negative[k] /= 1.0*num_0_negative + 1e-8;
	}
	for (int k = 0; k < Feature_Num_Level1; k++)
	{
		level1_mean_positive[k] /= 1.0*num_1_positive + 1e-8;
		level1_mean_negative[k] /= 1.0*num_1_negative + 1e-8;
	}
	for (int k = 0; k < Feature_Num_Level2; k++)
	{
		level2_mean_positive[k] /= 1.0*num_2_positive + 1e-8;
		level2_mean_negative[k] /= 1.0*num_2_negative + 1e-8;
	}
	//--------------------------------------------------------------------------
// 	for (int i=0;i<Feature_Num;i++)
// 	{
// 		cout<<M0[i].maxvalue<<" "<<M0[i].minvalue<<"|";
// 		cout<<M1[i].maxvalue<<" "<<M1[i].minvalue<<"|";
// 		cout<<M2[i].maxvalue<<" "<<M2[i].minvalue<<"|";
// 		cout<<M3[i].maxvalue<<" "<<M3[i].minvalue<<endl;
// 	}

	int count0, count1, count2;
	int negative_num0 = 0, negative_num1 = 0, negative_num2 = 0;
	int positive_num0 = 0, positive_num1 = 0, positive_num2 = 0;

	int negative_num00 = 0, negative_num11 = 0, negative_num22 = 0;
	int positive_num00 = 0, positive_num11 = 0, positive_num22 = 0;
	double weight_fuzzy = 0.00005;
	//1024*768/(64*64)=192
	int threshold_num0 = 192 * 80, threshold_num1 = 192 * 80, threshold_num2 = 192 * 80;

	for (int Class = 0; Class < 3; Class++)
	{
		int n = 0, k = 0;
		switch (Class)
		{
		case 0:
			for (int frame = 0; frame < trainingnum; frame++)
			{
				for (int i = 0; i < frameSize0; i++)
				{
					if (-1 == Truth0[frame*frameSize0 + i] && (i / W0 < H00&&i%W0 < W00))
					{
						negative_num0++;
					}
					else if (+1 == Truth0[frame*frameSize0 + i] && (i / W0 < H00&&i%W0 < W00))
					{
						positive_num0++;
					}
				}
			}
			count0 = (positive_num0 + negative_num0) >= threshold_num0 ? threshold_num0 : positive_num0 + negative_num0;
			prob0.l = count0;
			prob0.x = Malloc(svm_node *, prob0.l);
			prob0.y = Malloc(double, prob0.l);
			prob0.weight = Malloc(double, prob0.l);
			data0 = Malloc(svm_node, prob0.l*(Feature_Num_Level0 + 1));
			//-----------------------------------------------------------------------------------------------------
			for (int frame = 0; frame < trainingnum; frame++)
			{
				for (int i = 0; i < frameSize0; i++)
				{
					if (+1 == Truth0[frame*frameSize0 + i] && (i / W0 < H00&&i%W0 < W00))
					{
						prob0.y[n] = Truth0[frame*frameSize0 + i];
						prob0.x[n] = &data0[k];
						double temp_weight0 = 0;
						f0 << prob0.y[n] << " ";
						for (int j = 0; j < Feature_Num_Level0; j++)
						{
							data0[k].index = j + 1;
							f0 << j + 1 << ":";
							Double temp = Feature0[(frame*frameSize0 + i)*Feature_Num_Level0 + j];
							data0[k].value = -1 + 2 * (temp - M0[j].minvalue) / (M0[j].maxvalue - M0[j].minvalue + 1e-8);
							f0 << temp << " ";
							temp_weight0 += (data0[k].value - level0_mean_positive[j])*(data0[k].value - level0_mean_positive[j]);
							k++;
						}
						f0 << endl;
#if Fuzzy_SVM
						temp_weight0 = temp_weight0;
#else
						temp_weight0 = 0;
#endif
						prob0.weight[n] = 2.0 / (1.0 + exp(weight_fuzzy*temp_weight0));
						data0[k++].index = -1;
						n++;
						positive_num00++;
						if (n >= threshold_num0)
						{
							goto NEXT_00;
						}
					}
					else if (-1 == Truth0[frame*frameSize0 + i] && (i / W0 < H00&&i%W0 < W00))
					{
						prob0.y[n] = Truth0[frame*frameSize0 + i];
						prob0.x[n] = &data0[k];
						double temp_weight0 = 0;
						f0 << prob0.y[n] << " ";
						for (int j = 0; j < Feature_Num_Level0; j++)
						{
							data0[k].index = j + 1;
							f0 << j + 1 << ":";
							Double temp = Feature0[(frame*frameSize0 + i)*Feature_Num_Level0 + j];
							data0[k].value = -1 + 2 * (temp - M0[j].minvalue) / (M0[j].maxvalue - M0[j].minvalue + 1e-8);
							f0 << temp << " ";
							temp_weight0 += (data0[k].value - level0_mean_negative[j])*(data0[k].value - level0_mean_negative[j]);
							k++;
						}
						f0 << endl;
#if Fuzzy_SVM
						temp_weight0 = temp_weight0;
#else
						temp_weight0 = 0;
#endif
						prob0.weight[n] = 2.0 / (1.0 + exp(weight_fuzzy*temp_weight0));
						data0[k++].index = -1;
						n++;
						negative_num00++;
						if (n >= threshold_num0)
						{
							goto NEXT_00;
						}
					}
				}
			}
		NEXT_00:
			break;
			//-----------------------------------------------------------------------
		case 1:
			for (int frame = 0; frame < trainingnum; frame++)
			{
				for (int i = 0; i < frameSize0; i++)
				{
					int tempx = 2 * (i / W0), tempy = 2 * (i%W0);
					for (int l = 0; l < 4; l++)
					{
						int temp1 = W1 * (tempx + l / 2) + (tempy + l % 2);
						if (-1 == Truth1[4 * i + l + frame * frameSize1] && (temp1 / W1 < H11&&temp1%W1 < W11))
						{
							negative_num1++;
						}
						else if (+1 == Truth1[4 * i + l + frame * frameSize1] && (temp1 / W1 < H11&&temp1%W1 < W11))
						{
							positive_num1++;
						}
					}
				}
			}
			count1 = (positive_num1 + negative_num1) >= threshold_num1 ? threshold_num1 : positive_num1 + negative_num1;
			prob1.l = count1;
			prob1.x = Malloc(svm_node *, prob1.l);
			prob1.y = Malloc(double, prob1.l);
			prob1.weight = Malloc(double, prob1.l);
			data1 = Malloc(svm_node, prob1.l*(Feature_Num_Level1 + 1));
			//------------------------------------------------------------------------
			for (int frame = 0; frame < trainingnum; frame++)
			{
				for (int i = 0; i < frameSize0; i++)
				{
					int tempx = 2 * (i / W0), tempy = 2 * (i%W0);
					for (int l = 0; l < 4; l++)
					{
						int temp1 = W1 * (tempx + l / 2) + (tempy + l % 2);
						if (+1 == Truth1[4 * i + l + frame * frameSize1] && (temp1 / W1 < H11&&temp1%W1 < W11))
						{
							prob1.y[n] = Truth1[4 * i + l + frame * frameSize1];
							prob1.x[n] = &data1[k];
							double temp_weight1 = 0;
							f1 << prob1.y[n] << " ";
							for (int j = 0; j < Feature_Num_Level1; j++)
							{
								data1[k].index = j + 1;
								f1 << j + 1 << ":";
								double temp = Feature1[(frame*frameSize1 + temp1)*Feature_Num_Level1 + j];
								data1[k].value = -1 + 2 * (temp - M1[j].minvalue) / (M1[j].maxvalue - M1[j].minvalue + 1e-8);
								f1 << temp << " ";
								temp_weight1 += (data1[k].value - level1_mean_positive[j])*(data1[k].value - level1_mean_positive[j]);
								k++;
							}
							f1 << endl;
#if Fuzzy_SVM
							temp_weight1 = temp_weight1;
#else
							temp_weight1 = 0;
#endif
							prob1.weight[n] = 2.0 / (1.0 + exp(weight_fuzzy*temp_weight1));
							data1[k++].index = -1;
							n++;
							positive_num11++;
							if (n >= threshold_num1)
							{
								goto NEXT_10;
							}
						}
						else if (-1 == Truth1[4 * i + l + frame * frameSize1] && (temp1 / W1 < H11&&temp1%W1 < W11))
						{
							prob1.y[n] = Truth1[4 * i + l + frame * frameSize1];
							prob1.x[n] = &data1[k];
							double temp_weight1 = 0;
							f1 << prob1.y[n] << " ";
							for (int j = 0; j < Feature_Num_Level1; j++)
							{
								data1[k].index = j + 1;
								f1 << j + 1 << ":";
								double temp = Feature1[(frame*frameSize1 + temp1)*Feature_Num_Level1 + j];
								data1[k].value = -1 + 2 * (temp - M1[j].minvalue) / (M1[j].maxvalue - M1[j].minvalue + 1e-8);
								f1 << temp << " ";
								temp_weight1 += (data1[k].value - level1_mean_negative[j])*(data1[k].value - level1_mean_negative[j]);
								k++;
							}
							f1 << endl;
#if Fuzzy_SVM
							temp_weight1 = temp_weight1;
#else
							temp_weight1 = 0;
#endif
							prob1.weight[n] = 2.0 / (1.0 + exp(weight_fuzzy*temp_weight1));
							data1[k++].index = -1;
							n++;
							negative_num11++;
							if (n >= threshold_num1)
							{
								goto NEXT_10;
							}
						}
					}
				}
			}
		NEXT_10:
			break;
		case 2:
			for (int frame = 0; frame < trainingnum; frame++)
			{
				for (int i = 0; i < frameSize0; i++)
				{
					int tempx = 2 * (i / W0), tempy = 2 * (i%W0);
					for (int l = 0; l < 4; l++)
					{
						int temp1 = W1 * (tempx + l / 2) + (tempy + l % 2);
						int tempxx = 2 * (temp1 / W1), tempyy = 2 * (temp1%W1);
						for (int m = 0; m < 4; m++)
						{
							int temp2 = W2 * (tempxx + m / 2) + (tempyy + m % 2);
							if (-1 == Truth2[4 * (4 * i + l) + m + frame * frameSize2] && (temp2 / W2 < H22&&temp2%W2 < W22))
							{
								negative_num2++;
							}
							else if (+1 == Truth2[4 * (4 * i + l) + m + frame * frameSize2] && (temp2 / W2 < H22&&temp2%W2 < W22))
							{
								positive_num2++;
							}
						}
					}
				}
			}
			count2 = (positive_num2 + negative_num2) >= threshold_num2 ? threshold_num2 : positive_num2 + negative_num2;
			prob2.l = count2;
			prob2.x = Malloc(svm_node *, prob2.l);
			prob2.y = Malloc(double, prob2.l);
			prob2.weight = Malloc(double, prob2.l);
			data2 = Malloc(svm_node, prob2.l*(Feature_Num_Level2 + 1));
			//-------------------------------------------------------------
			for (int frame = 0; frame < trainingnum; frame++)
			{
				for (int i = 0; i < frameSize0; i++)
				{
					int tempx = 2 * (i / W0), tempy = 2 * (i%W0);
					for (int l = 0; l < 4; l++)
					{
						int tempxx = 2 * tempx + 2 * (l / 2), tempyy = 2 * tempy + 2 * (l % 2);
						for (int m = 0; m < 4; m++)
						{
							int temp2 = W2 * (tempxx + m / 2) + (tempyy + m % 2);
							if (+1 == Truth2[4 * (4 * i + l) + m + frame * frameSize2] && (temp2 / W2 < H22&&temp2%W2 < W22))
							{
								prob2.y[n] = Truth2[4 * (4 * i + l) + m + frame * frameSize2];
								prob2.x[n] = &data2[k];
								double temp_weight2 = 0;
								f2 << prob2.y[n] << " ";
								for (int j = 0; j < Feature_Num_Level2; j++)
								{
									data2[k].index = j + 1;
									f2 << j + 1 << ":";
									double temp = Feature2[(frame*frameSize2 + temp2)*Feature_Num_Level2 + j];
									data2[k].value = -1 + 2 * (temp - M2[j].minvalue) / (M2[j].maxvalue - M2[j].minvalue + 1e-8);
									f2 << temp << " ";
									temp_weight2 += (data2[k].value - level2_mean_positive[j])*(data2[k].value - level2_mean_positive[j]);
									k++;
								}
								f2 << endl;
#if Fuzzy_SVM
								temp_weight2 = temp_weight2;
#else
								temp_weight2 = 0;
#endif
								prob2.weight[n] = 2.0 / (1.0 + exp(weight_fuzzy*temp_weight2));
								data2[k++].index = -1;
								n++;
								positive_num22++;;
								if (n >= threshold_num2)
								{
									goto NEXT_20;
								}
							}
							else if (-1 == Truth2[4 * (4 * i + l) + m + frame * frameSize2] && (temp2 / W2 < H22&&temp2%W2 < W22))
							{
								prob2.y[n] = Truth2[4 * (4 * i + l) + m + frame * frameSize2];
								prob2.x[n] = &data2[k];
								double temp_weight2 = 0;
								f2 << prob2.y[n] << " ";
								for (int j = 0; j < Feature_Num_Level2; j++)
								{
									data2[k].index = j + 1;
									f2 << j + 1 << ":";
									double temp = Feature2[(frame*frameSize2 + temp2)*Feature_Num_Level2 + j];
									data2[k].value = -1 + 2 * (temp - M2[j].minvalue) / (M2[j].maxvalue - M2[j].minvalue + 1e-8);
									f2 << temp << " ";
									temp_weight2 += (data2[k].value - level2_mean_negative[j])*(data2[k].value - level2_mean_negative[j]);
									k++;
								}
								f2 << endl;
#if Fuzzy_SVM
								temp_weight2 = temp_weight2;
#else
								temp_weight2 = 0;
#endif
								prob2.weight[n] = 2.0 / (1.0 + exp(weight_fuzzy*temp_weight2));
								data2[k++].index = -1;
								n++;
								negative_num22++;
								if (n >= threshold_num2)
								{
									goto NEXT_20;
								}
							}
						}
					}
				}
			}
		NEXT_20:
			break;
		}
		//=====================================================================================================
		const char *error_msg = NULL;
		switch (Class)
		{
		case 0:
			//param.gamma=1.0/Feature_Num_Level0;
#if Different_Misclassification_Cost
			param.weight[0] = 1;
			param.weight[1] = 4.5;//LXY

#else
			param.weight[0] = 1;
			param.weight[1] = 1;//LXY
#endif
			if (negative_num00 > 2 * positive_num00)
			{
				param.weight[0] = param.weight[0] * (2 + 1) / 2;
				param.weight[1] = param.weight[1] * (2 + 1) / 1;//LXY
			}
			//else if (positive_num00 > 2 * negative_num0)
			else if (positive_num00 > 2 * negative_num00)// LXY修改
			{
				param.weight[0] = param.weight[0] * (2 + 1) / 1;
				param.weight[1] = param.weight[1] * (2 + 1) / 2;//LXY
			}

			if (negative_num0 == 0 || positive_num0 == 0)
			{
				param.weight[0] = 1.0;
				param.weight[1] = 1.0;//LXY
			}

			error_msg = svm_check_parameter(&prob0, &param);
			if (error_msg)
			{
				fprintf(stderr, "ERROR: %s\n", error_msg);
				exit(1);
			}
			model0 = svm_train(&prob0, &param);
			svm_save_model("model0.txt", model0);
			free(prob0.x);
			free(prob0.y);
			free(prob0.weight);
			//free(data0);
			break;
		case 1:
			//param.gamma=1.0/Feature_Num_Level1;
#if Different_Misclassification_Cost
			param.weight[0] = 1;
			param.weight[1] = 1.5;//LXY

#else
			param.weight[0] = 1;
			param.weight[1] = 1;//LXY
#endif
			if (negative_num11 > 2 * positive_num11)
			{
				param.weight[0] = param.weight[0] * (2 + 1) / 2;
				param.weight[1] = param.weight[1] * (2 + 1) / 1;//LXY
			}
			else if (positive_num11 > 2 * negative_num11)
			{
				param.weight[0] = param.weight[0] * (2 + 1) / 1;
				param.weight[1] = param.weight[1] * (2 + 1) / 2;//LXY
			}

			if (negative_num1 == 0 || positive_num1 == 0)
			{
				param.weight[0] = 1.0;
				param.weight[1] = 1.0;//LXY
			}

			error_msg = svm_check_parameter(&prob1, &param);
			if (error_msg)
			{
				fprintf(stderr, "ERROR: %s\n", error_msg);
				exit(1);
			}
			model1 = svm_train(&prob1, &param);
			svm_save_model("model1.txt", model1);
			free(prob1.x);
			free(prob1.y);
			free(prob1.weight);
			//free(data1);
			break;
		case 2:
			//param.gamma=1.0/Feature_Num_Level2;
#if Different_Misclassification_Cost
			param.weight[0] = 1;
			param.weight[1] = 1.5;//LXY

#else
			param.weight[0] = 1;
			param.weight[1] = 1;//LXY
#endif
			if (negative_num22 > 2 * positive_num22)
			{
				param.weight[0] = param.weight[0] * (2 + 1) / 2;
				param.weight[1] = param.weight[1] * (2 + 1) / 1;//LXY
			}
			else if (positive_num22 > 2 * negative_num22)
			{
				param.weight[0] = param.weight[0] * (2 + 1) / 1;
				param.weight[1] = param.weight[1] * (2 + 1) / 2;//LXY
			}

			if (negative_num2 == 0 || positive_num2 == 0)
			{
				param.weight[0] = 1.0;
				param.weight[1] = 1.0;//LXY
			}

			error_msg = svm_check_parameter(&prob2, &param);
			if (error_msg)
			{
				fprintf(stderr, "ERROR: %s\n", error_msg);
				exit(1);
			}
			model2 = svm_train(&prob2, &param);
			svm_save_model("model2.txt", model2);
			free(prob2.x);
			free(prob2.y);
			free(prob2.weight);
			//free(data2);
			break;
		}
	}
	//--------------------------------------------------------------------------

#if PUMode_FastDecision
	//--------------------------------------------------------------------------
	for (Int j = 0; j < Feature_Num_Level0; j++)
	{
		M0[j].maxvalue = Feature0[j];
		M0[j].minvalue = Feature0[j];
	}
	for (Int j = 0; j < Feature_Num_Level1; j++)
	{
		M1[j].maxvalue = Feature1[j];
		M1[j].minvalue = Feature1[j];
	}
	for (Int j = 0; j < Feature_Num_Level2; j++)
	{
		M2[j].maxvalue = Feature2[j];
		M2[j].minvalue = Feature2[j];
	}


	// W00 = WIDTH / 64, H00 = HEIGHT / 64;
	// W11 = WIDTH / 32, H11 = HEIGHT / 32;
	// W22 = WIDTH / 16, H22 = HEIGHT / 16;

	for (int i = 0; i < trainingnum; i++)
	{
		for (int j = 0; j < frameSize0; j++)
		{
			if (0 != Truth0_PU[i*frameSize0 + j] && (j / W0 < H00&&j%W0 < W00))
			{
				for (int k = 0; k < Feature_Num_Level0; k++)
				{
					if (Feature0[(i*frameSize0 + j)*Feature_Num_Level0 + k] >= M0[k].maxvalue)
						M0[k].maxvalue = Feature0[(i*frameSize0 + j)*Feature_Num_Level0 + k];
					else if (Feature0[(i*frameSize0 + j)*Feature_Num_Level0 + k] <= M0[k].minvalue)
						M0[k].minvalue = Feature0[(i*frameSize0 + j)*Feature_Num_Level0 + k];
				}
			}
		}
	}

	for (int frame = 0; frame < trainingnum; frame++)
	{
		for (int i = 0; i < frameSize0; i++)
		{
			int tempx = 2 * (i / W0), tempy = 2 * (i%W0);
			for (int l = 0; l < 4; l++)
			{
				int temp1 = W1 * (tempx + l / 2) + (tempy + l % 2);
				if (0 != Truth1_PU[4 * i + l + frame * frameSize1] && (temp1 / W1 < H11&&temp1%W1 < W11))
				{
					for (int j = 0; j < Feature_Num_Level1; j++)
					{
						if (Feature1[(frame*frameSize1 + temp1)*Feature_Num_Level1 + j] >= M1[j].maxvalue)     M1[j].maxvalue = Feature1[(frame*frameSize1 + temp1)*Feature_Num_Level1 + j];
						else if (Feature1[(frame*frameSize1 + temp1)*Feature_Num_Level1 + j] <= M1[j].minvalue) M1[j].minvalue = Feature1[(frame*frameSize1 + temp1)*Feature_Num_Level1 + j];
					}
				}
			}
		}
	}

	for (int frame = 0; frame < trainingnum; frame++)
	{
		for (int i = 0; i < frameSize0; i++)
		{
			int tempx = 2 * (i / W0), tempy = 2 * (i%W0);
			for (int l = 0; l < 4; l++)
			{
				int temp1 = W1 * (tempx + l / 2) + (tempy + l % 2);
				int tempxx = 2 * (temp1 / W1), tempyy = 2 * (temp1%W1);
				for (int m = 0; m < 4; m++)
				{
					int temp2 = W2 * (tempxx + m / 2) + (tempyy + m % 2);
					if (0 != Truth2_PU[4 * (4 * i + l) + m + frame * frameSize2] && (temp2 / W2 < H22&&temp2%W2 < W22))
					{
						for (int j = 0; j < Feature_Num_Level2; j++)
						{
							if (Feature2[(frame*frameSize2 + temp2)*Feature_Num_Level2 + j] >= M2[j].maxvalue)     M2[j].maxvalue = Feature2[(frame*frameSize2 + temp2)*Feature_Num_Level2 + j];
							else if (Feature2[(frame*frameSize2 + temp2)*Feature_Num_Level2 + j] <= M2[j].minvalue) M2[j].minvalue = Feature2[(frame*frameSize2 + temp2)*Feature_Num_Level2 + j];
						}
					}
				}

			}
		}
	}
	//=========================================================================//
	memset(level0_mean_positive, 0, sizeof(double)*Feature_Num_Level0);
	memset(level1_mean_positive, 0, sizeof(double)*Feature_Num_Level1);
	memset(level2_mean_positive, 0, sizeof(double)*Feature_Num_Level2);

	memset(level0_mean_negative, 0, sizeof(double)*Feature_Num_Level0);
	memset(level1_mean_negative, 0, sizeof(double)*Feature_Num_Level1);
	memset(level2_mean_negative, 0, sizeof(double)*Feature_Num_Level2);

	num_0_positive = 0, num_1_positive = 0, num_2_positive = 0;
	num_0_negative = 0, num_1_negative = 0, num_2_negative = 0;

	for (int i = 0; i < trainingnum; i++)
	{
		for (int j = 0; j < frameSize0; j++)
		{
			if (+1 == Truth0_PU[i*frameSize0 + j] && (j / W0 < H00&&j%W0 < W00))
			{
				for (int k = 0; k < Feature_Num_Level0; k++)
				{
					double temp = Feature0[(i*frameSize0 + j)*Feature_Num_Level0 + k];
					level0_mean_positive[k] += -1 + 2.0*(temp - M0[k].minvalue) / (M0[k].maxvalue - M0[k].minvalue + 1e-8);
				}
				num_0_positive++;
			}
			else if (-1 == Truth0_PU[i*frameSize0 + j] && (j / W0 < H00&&j%W0 < W00))
			{
				for (int k = 0; k < Feature_Num_Level0; k++)
				{
					double temp = Feature0[(i*frameSize0 + j)*Feature_Num_Level0 + k];
					level0_mean_negative[k] += -1 + 2.0*(temp - M0[k].minvalue) / (M0[k].maxvalue - M0[k].minvalue + 1e-8);
				}
				num_0_negative++;
			}
		}
	}

	for (int frame = 0; frame < trainingnum; frame++)
	{
		for (int i = 0; i < frameSize0; i++)
		{
			int tempx = 2 * (i / W0), tempy = 2 * (i%W0);
			for (int l = 0; l < 4; l++)
			{
				int temp1 = W1 * (tempx + l / 2) + (tempy + l % 2);
				if (+1 == Truth1_PU[4 * i + l + frame * frameSize1] && (temp1 / W1 < H11&&temp1%W1 < W11))
				{
					for (int j = 0; j < Feature_Num_Level1; j++)
					{
						double temp = Feature1[(frame*frameSize1 + temp1)*Feature_Num_Level1 + j];
						level1_mean_positive[j] += -1 + 2.0*(temp - M1[j].minvalue) / (M1[j].maxvalue - M1[j].minvalue + 1e-8);
					}
					num_1_positive++;
				}
				else if (-1 == Truth1_PU[4 * i + l + frame * frameSize1] && (temp1 / W1 < H11&&temp1%W1 < W11))
				{
					for (int j = 0; j < Feature_Num_Level1; j++)
					{
						double temp = Feature1[(frame*frameSize1 + temp1)*Feature_Num_Level1 + j];
						level1_mean_negative[j] += -1 + 2.0*(temp - M1[j].minvalue) / (M1[j].maxvalue - M1[j].minvalue + 1e-8);
					}
					num_1_negative++;
				}
			}
		}
	}

	for (int frame = 0; frame < trainingnum; frame++)
	{
		for (int i = 0; i < frameSize0; i++)
		{
			int tempx = 2 * (i / W0), tempy = 2 * (i%W0);
			for (int l = 0; l < 4; l++)
			{
				int temp1 = W1 * (tempx + l / 2) + (tempy + l % 2);
				int tempxx = 2 * (temp1 / W1), tempyy = 2 * (temp1%W1);
				for (int m = 0; m < 4; m++)
				{
					int temp2 = W2 * (tempxx + m / 2) + (tempyy + m % 2);
					if (+1 == Truth2_PU[4 * (4 * i + l) + m + frame * frameSize2] && (temp2 / W2 < H22&&temp2%W2 < W22))
					{
						for (int j = 0; j < Feature_Num_Level2; j++)
						{
							double temp = Feature2[(frame*frameSize2 + temp2)*Feature_Num_Level2 + j];
							level2_mean_positive[j] += -1 + 2.0*(temp - M2[j].minvalue) / (M2[j].maxvalue - M2[j].minvalue + 1e-8);
						}
						num_2_positive++;
					}
					else if (-1 == Truth2_PU[4 * (4 * i + l) + m + frame * frameSize2] && (temp2 / W2 < H22&&temp2%W2 < W22))
					{
						for (int j = 0; j < Feature_Num_Level2; j++)
						{
							double temp = Feature2[(frame*frameSize2 + temp2)*Feature_Num_Level2 + j];
							level2_mean_negative[j] += -1 + 2.0*(temp - M2[j].minvalue) / (M2[j].maxvalue - M2[j].minvalue + 1e-8);
						}
						num_2_negative++;
					}
				}
			}
		}
	}

	for (int k = 0; k < Feature_Num_Level0; k++)
	{
		level0_mean_positive[k] /= 1.0*num_0_positive + 1e-8;
		level0_mean_negative[k] /= 1.0*num_0_negative + 1e-8;
	}
	for (int k = 0; k < Feature_Num_Level1; k++)
	{
		level1_mean_positive[k] /= 1.0*num_1_positive + 1e-8;
		level1_mean_negative[k] /= 1.0*num_1_negative + 1e-8;
	}
	for (int k = 0; k < Feature_Num_Level2; k++)
	{
		level2_mean_positive[k] /= 1.0*num_2_positive + 1e-8;
		level2_mean_negative[k] /= 1.0*num_2_negative + 1e-8;
	}
	//--------------------------------------------------------------------------
// 	for (int i=0;i<Feature_Num;i++)
// 	{
// 		cout<<M0[i].maxvalue<<" "<<M0[i].minvalue<<"|";
// 		cout<<M1[i].maxvalue<<" "<<M1[i].minvalue<<"|";
// 		cout<<M2[i].maxvalue<<" "<<M2[i].minvalue<<"|";
// 		cout<<M3[i].maxvalue<<" "<<M3[i].minvalue<<endl;
// 	}

	count0 = 0, count1 = 0, count2 = 0;
	negative_num0 = 0, negative_num1 = 0, negative_num2 = 0;
	positive_num0 = 0, positive_num1 = 0, positive_num2 = 0;

	negative_num00 = 0, negative_num11 = 0, negative_num22 = 0;
	positive_num00 = 0, positive_num11 = 0, positive_num22 = 0;
	weight_fuzzy = 0.00005;
	//1024*768/(64*64)=192
	threshold_num0 = 192 * 80, threshold_num1 = 192 * 80, threshold_num2 = 192 * 80;

	for (int Class = 0; Class < 3; Class++)
	{
		int n = 0, k = 0;
		switch (Class)
		{
		case 0:
			for (int frame = 0; frame < trainingnum; frame++)
			{
				for (int i = 0; i < frameSize0; i++)
				{
					if (-1 == Truth0_PU[frame*frameSize0 + i] && (i / W0 < H00&&i%W0 < W00))
					{
						negative_num0++;
					}
					else if (+1 == Truth0_PU[frame*frameSize0 + i] && (i / W0 < H00&&i%W0 < W00))
					{
						positive_num0++;
					}
				}
			}
			count0 = (positive_num0 + negative_num0) >= threshold_num0 ? threshold_num0 : positive_num0 + negative_num0;
			prob0_PU.l = count0;
			prob0_PU.x = Malloc(svm_node *, prob0_PU.l);
			prob0_PU.y = Malloc(double, prob0_PU.l);
			prob0_PU.weight = Malloc(double, prob0_PU.l);
			data0_PU = Malloc(svm_node, prob0_PU.l*(Feature_Num_Level0 + 1));
			//-----------------------------------------------------------------------------------------------------
			for (int frame = 0; frame < trainingnum; frame++)
			{
				for (int i = 0; i < frameSize0; i++)
				{
					if (+1 == Truth0_PU[frame*frameSize0 + i] && (i / W0 < H00&&i%W0 < W00))
					{
						prob0_PU.y[n] = Truth0_PU[frame*frameSize0 + i];
						prob0_PU.x[n] = &data0_PU[k];
						double temp_weight0 = 0;
						f0 << prob0_PU.y[n] << " ";
						for (int j = 0; j < Feature_Num_Level0; j++)
						{
							data0_PU[k].index = j + 1;
							f0 << j + 1 << ":";
							Double temp = Feature0[(frame*frameSize0 + i)*Feature_Num_Level0 + j];
							data0_PU[k].value = -1 + 2 * (temp - M0[j].minvalue) / (M0[j].maxvalue - M0[j].minvalue + 1e-8);
							f0 << temp << " ";
							temp_weight0 += (data0_PU[k].value - level0_mean_positive[j])*(data0_PU[k].value - level0_mean_positive[j]);
							k++;
						}
						f0 << endl;
#if Fuzzy_SVM
						temp_weight0 = temp_weight0;
#else
						temp_weight0 = 0;
#endif
						prob0_PU.weight[n] = 2.0 / (1.0 + exp(weight_fuzzy*temp_weight0));
						data0_PU[k++].index = -1;
						n++;
						positive_num00++;
						if (n >= threshold_num0)
						{
							goto NEXT_00_PU;
						}
					}
					else if (-1 == Truth0_PU[frame*frameSize0 + i] && (i / W0 < H00&&i%W0 < W00))
					{
						prob0_PU.y[n] = Truth0_PU[frame*frameSize0 + i];
						prob0_PU.x[n] = &data0_PU[k];
						double temp_weight0 = 0;
						f0 << prob0_PU.y[n] << " ";
						for (int j = 0; j < Feature_Num_Level0; j++)
						{
							data0_PU[k].index = j + 1;
							f0 << j + 1 << ":";
							Double temp = Feature0[(frame*frameSize0 + i)*Feature_Num_Level0 + j];
							data0_PU[k].value = -1 + 2 * (temp - M0[j].minvalue) / (M0[j].maxvalue - M0[j].minvalue + 1e-8);
							f0 << temp << " ";
							temp_weight0 += (data0_PU[k].value - level0_mean_negative[j])*(data0_PU[k].value - level0_mean_negative[j]);
							k++;
						}
						f0 << endl;
#if Fuzzy_SVM
						temp_weight0 = temp_weight0;
#else
						temp_weight0 = 0;
#endif
						prob0_PU.weight[n] = 2.0 / (1.0 + exp(weight_fuzzy*temp_weight0));
						data0_PU[k++].index = -1;
						n++;
						negative_num00++;
						if (n >= threshold_num0)
						{
							goto NEXT_00_PU;
						}
					}
				}
			}
		NEXT_00_PU:
			break;
			//-----------------------------------------------------------------------
		case 1:
			for (int frame = 0; frame < trainingnum; frame++)
			{
				for (int i = 0; i < frameSize0; i++)
				{
					int tempx = 2 * (i / W0), tempy = 2 * (i%W0);
					for (int l = 0; l < 4; l++)
					{
						int temp1 = W1 * (tempx + l / 2) + (tempy + l % 2);
						if (-1 == Truth1_PU[4 * i + l + frame * frameSize1] && (temp1 / W1 < H11&&temp1%W1 < W11))
						{
							negative_num1++;
						}
						else if (+1 == Truth1_PU[4 * i + l + frame * frameSize1] && (temp1 / W1 < H11&&temp1%W1 < W11))
						{
							positive_num1++;
						}
					}
				}
			}
			count1 = (positive_num1 + negative_num1) >= threshold_num1 ? threshold_num1 : positive_num1 + negative_num1;
			prob1_PU.l = count1;
			prob1_PU.x = Malloc(svm_node *, prob1_PU.l);
			prob1_PU.y = Malloc(double, prob1_PU.l);
			prob1_PU.weight = Malloc(double, prob1_PU.l);
			data1_PU = Malloc(svm_node, prob1_PU.l*(Feature_Num_Level1 + 1));
			//------------------------------------------------------------------------
			for (int frame = 0; frame < trainingnum; frame++)
			{
				for (int i = 0; i < frameSize0; i++)
				{
					int tempx = 2 * (i / W0), tempy = 2 * (i%W0);
					for (int l = 0; l < 4; l++)
					{
						int temp1 = W1 * (tempx + l / 2) + (tempy + l % 2);
						if (+1 == Truth1_PU[4 * i + l + frame * frameSize1] && (temp1 / W1 < H11&&temp1%W1 < W11))
						{
							prob1_PU.y[n] = Truth1_PU[4 * i + l + frame * frameSize1];
							prob1_PU.x[n] = &data1_PU[k];
							double temp_weight1 = 0;
							f1 << prob1_PU.y[n] << " ";
							for (int j = 0; j < Feature_Num_Level1; j++)
							{
								data1_PU[k].index = j + 1;
								f1 << j + 1 << ":";
								double temp = Feature1[(frame*frameSize1 + temp1)*Feature_Num_Level1 + j];
								data1_PU[k].value = -1 + 2 * (temp - M1[j].minvalue) / (M1[j].maxvalue - M1[j].minvalue + 1e-8);
								f1 << temp << " ";
								temp_weight1 += (data1_PU[k].value - level1_mean_positive[j])*(data1_PU[k].value - level1_mean_positive[j]);
								k++;
							}
							f1 << endl;
#if Fuzzy_SVM
							temp_weight1 = temp_weight1;
#else
							temp_weight1 = 0;
#endif
							prob1_PU.weight[n] = 2.0 / (1.0 + exp(weight_fuzzy*temp_weight1));
							data1_PU[k++].index = -1;
							n++;
							positive_num11++;
							if (n >= threshold_num1)
							{
								goto NEXT_10_PU;
							}
						}
						else if (-1 == Truth1_PU[4 * i + l + frame * frameSize1] && (temp1 / W1 < H11&&temp1%W1 < W11))
						{
							prob1_PU.y[n] = Truth1_PU[4 * i + l + frame * frameSize1];
							prob1_PU.x[n] = &data1_PU[k];
							double temp_weight1 = 0;
							f1 << prob1_PU.y[n] << " ";
							for (int j = 0; j < Feature_Num_Level1; j++)
							{
								data1_PU[k].index = j + 1;
								f1 << j + 1 << ":";
								double temp = Feature1[(frame*frameSize1 + temp1)*Feature_Num_Level1 + j];
								data1_PU[k].value = -1 + 2 * (temp - M1[j].minvalue) / (M1[j].maxvalue - M1[j].minvalue + 1e-8);
								f1 << temp << " ";
								temp_weight1 += (data1_PU[k].value - level1_mean_negative[j])*(data1_PU[k].value - level1_mean_negative[j]);
								k++;
							}
							f1 << endl;
#if Fuzzy_SVM
							temp_weight1 = temp_weight1;
#else
							temp_weight1 = 0;
#endif
							prob1_PU.weight[n] = 2.0 / (1.0 + exp(weight_fuzzy*temp_weight1));
							data1_PU[k++].index = -1;
							n++;
							negative_num11++;
							if (n >= threshold_num1)
							{
								goto NEXT_10_PU;
							}
						}
					}
				}
			}
		NEXT_10_PU:
			break;
		case 2:
			for (int frame = 0; frame < trainingnum; frame++)
			{
				for (int i = 0; i < frameSize0; i++)
				{
					int tempx = 2 * (i / W0), tempy = 2 * (i%W0);
					for (int l = 0; l < 4; l++)
					{
						int temp1 = W1 * (tempx + l / 2) + (tempy + l % 2);
						int tempxx = 2 * (temp1 / W1), tempyy = 2 * (temp1%W1);
						for (int m = 0; m < 4; m++)
						{
							int temp2 = W2 * (tempxx + m / 2) + (tempyy + m % 2);
							if (-1 == Truth2_PU[4 * (4 * i + l) + m + frame * frameSize2] && (temp2 / W2 < H22&&temp2%W2 < W22))
							{
								negative_num2++;
							}
							else if (+1 == Truth2_PU[4 * (4 * i + l) + m + frame * frameSize2] && (temp2 / W2 < H22&&temp2%W2 < W22))
							{
								positive_num2++;
							}
						}
					}
				}
			}
			count2 = (positive_num2 + negative_num2) >= threshold_num2 ? threshold_num2 : positive_num2 + negative_num2;
			prob2_PU.l = count2;
			prob2_PU.x = Malloc(svm_node *, prob2_PU.l);
			prob2_PU.y = Malloc(double, prob2_PU.l);
			prob2_PU.weight = Malloc(double, prob2_PU.l);
			data2_PU = Malloc(svm_node, prob2_PU.l*(Feature_Num_Level2 + 1));
			//-------------------------------------------------------------
			for (int frame = 0; frame < trainingnum; frame++)
			{
				for (int i = 0; i < frameSize0; i++)
				{
					int tempx = 2 * (i / W0), tempy = 2 * (i%W0);
					for (int l = 0; l < 4; l++)
					{
						int tempxx = 2 * tempx + 2 * (l / 2), tempyy = 2 * tempy + 2 * (l % 2);
						for (int m = 0; m < 4; m++)
						{
							int temp2 = W2 * (tempxx + m / 2) + (tempyy + m % 2);
							if (+1 == Truth2_PU[4 * (4 * i + l) + m + frame * frameSize2] && (temp2 / W2 < H22&&temp2%W2 < W22))
							{
								prob2_PU.y[n] = Truth2_PU[4 * (4 * i + l) + m + frame * frameSize2];
								prob2_PU.x[n] = &data2_PU[k];
								double temp_weight2 = 0;
								f2 << prob2_PU.y[n] << " ";
								for (int j = 0; j < Feature_Num_Level2; j++)
								{
									data2_PU[k].index = j + 1;
									f2 << j + 1 << ":";
									double temp = Feature2[(frame*frameSize2 + temp2)*Feature_Num_Level2 + j];
									data2_PU[k].value = -1 + 2 * (temp - M2[j].minvalue) / (M2[j].maxvalue - M2[j].minvalue + 1e-8);
									f2 << temp << " ";
									temp_weight2 += (data2_PU[k].value - level2_mean_positive[j])*(data2_PU[k].value - level2_mean_positive[j]);
									k++;
								}
								f2 << endl;
#if Fuzzy_SVM
								temp_weight2 = temp_weight2;
#else
								temp_weight2 = 0;
#endif
								prob2_PU.weight[n] = 2.0 / (1.0 + exp(weight_fuzzy*temp_weight2));
								data2_PU[k++].index = -1;
								n++;
								positive_num22++;;
								if (n >= threshold_num2)
								{
									goto NEXT_20_PU;
								}
							}
							else if (-1 == Truth2_PU[4 * (4 * i + l) + m + frame * frameSize2] && (temp2 / W2 < H22&&temp2%W2 < W22))
							{
								prob2_PU.y[n] = Truth2_PU[4 * (4 * i + l) + m + frame * frameSize2];
								prob2_PU.x[n] = &data2_PU[k];
								double temp_weight2 = 0;
								f2 << prob2_PU.y[n] << " ";
								for (int j = 0; j < Feature_Num_Level2; j++)
								{
									data2_PU[k].index = j + 1;
									f2 << j + 1 << ":";
									double temp = Feature2[(frame*frameSize2 + temp2)*Feature_Num_Level2 + j];
									data2_PU[k].value = -1 + 2 * (temp - M2[j].minvalue) / (M2[j].maxvalue - M2[j].minvalue + 1e-8);
									f2 << temp << " ";
									temp_weight2 += (data2_PU[k].value - level2_mean_negative[j])*(data2_PU[k].value - level2_mean_negative[j]);
									k++;
								}
								f2 << endl;
#if Fuzzy_SVM
								temp_weight2 = temp_weight2;
#else
								temp_weight2 = 0;
#endif
								prob2_PU.weight[n] = 2.0 / (1.0 + exp(weight_fuzzy*temp_weight2));
								data2_PU[k++].index = -1;
								n++;
								negative_num22++;
								if (n >= threshold_num2)
								{
									goto NEXT_20_PU;
								}
							}
						}
					}
				}
			}
		NEXT_20_PU:
			break;
		}
		//=====================================================================================================
		const char *error_msg = NULL;
		switch (Class)
		{
		case 0:
			//param.gamma=1.0/Feature_Num_Level0;
#if Different_Misclassification_Cost
			param.weight[0] = 1;
			param.weight[1] = 4.5;//LXY

#else
			param.weight[0] = 1;
			param.weight[1] = 1;//LXY
#endif
			if (negative_num00 > 2 * positive_num00)
			{
				param.weight[0] = param.weight[0] * (2 + 1) / 2;
				param.weight[1] = param.weight[1] * (2 + 1) / 1;//LXY
			}
			else if (positive_num00 > 2 * negative_num00)
			{
				param.weight[0] = param.weight[0] * (2 + 1) / 1;
				param.weight[1] = param.weight[1] * (2 + 1) / 2;//LXY
			}

			if (negative_num0 == 0 || positive_num0 == 0)
			{
				param.weight[0] = 1.0;
				param.weight[1] = 1.0;//LXY
			}

			error_msg = svm_check_parameter(&prob0_PU, &param);
			if (error_msg)
			{
				fprintf(stderr, "ERROR: %s\n", error_msg);
				exit(1);
			}
			model0_PU = svm_train(&prob0_PU, &param);
			svm_save_model("model0.txt", model0_PU);
			free(prob0_PU.x);
			free(prob0_PU.y);
			free(prob0_PU.weight);
			//free(data0);
			break;
		case 1:
			//param.gamma=1.0/Feature_Num_Level1;
#if Different_Misclassification_Cost
			param.weight[0] = 1;
			param.weight[1] = 1.5;//LXY

#else
			param.weight[0] = 1;
			param.weight[1] = 1;//LXY
#endif
			if (negative_num11 > 2 * positive_num11)
			{
				param.weight[0] = param.weight[0] * (2 + 1) / 2;
				param.weight[1] = param.weight[1] * (2 + 1) / 1;//LXY
			}
			else if (positive_num11 > 2 * negative_num11)
			{
				param.weight[0] = param.weight[0] * (2 + 1) / 1;
				param.weight[1] = param.weight[1] * (2 + 1) / 2;//LXY
			}

			if (negative_num1 == 0 || positive_num1 == 0)
			{
				param.weight[0] = 1.0;
				param.weight[1] = 1.0;//LXY
			}

			error_msg = svm_check_parameter(&prob1_PU, &param);
			if (error_msg)
			{
				fprintf(stderr, "ERROR: %s\n", error_msg);
				exit(1);
			}
			model1_PU = svm_train(&prob1_PU, &param);
			svm_save_model("model1.txt", model1_PU);
			free(prob1_PU.x);
			free(prob1_PU.y);
			free(prob1_PU.weight);
			//free(data1);
			break;
		case 2:
			//param.gamma=1.0/Feature_Num_Level2;
#if Different_Misclassification_Cost
			param.weight[0] = 1;
			param.weight[1] = 1.5;//LXY

#else
			param.weight[0] = 1;
			param.weight[1] = 1;//LXY
#endif
			if (negative_num22 > 2 * positive_num22)
			{
				param.weight[0] = param.weight[0] * (2 + 1) / 2;
				param.weight[1] = param.weight[1] * (2 + 1) / 1;//LXY
			}
			else if (positive_num22 > 2 * negative_num22)
			{
				param.weight[0] = param.weight[0] * (2 + 1) / 1;
				param.weight[1] = param.weight[1] * (2 + 1) / 2;//LXY
			}

			if (negative_num2 == 0 || positive_num2 == 0)
			{
				param.weight[0] = 1.0;
				param.weight[1] = 1.0;//LXY
			}

			error_msg = svm_check_parameter(&prob2_PU, &param);
			if (error_msg)
			{
				fprintf(stderr, "ERROR: %s\n", error_msg);
				exit(1);
			}
			model2_PU = svm_train(&prob2_PU, &param);
			svm_save_model("model2.txt", model2_PU);
			free(prob2_PU.x);
			free(prob2_PU.y);
			free(prob2_PU.weight);
			//free(data2);
			break;
		}
	}
#endif

	delete[] level0_mean_positive, level1_mean_positive, level2_mean_positive,
		level0_mean_negative, level1_mean_negative, level2_mean_negative;

}
#endif

Void TAppEncTop::xInitLibCfg()
{
#if SVC_EXTENSION
	TComVPS& vps = *m_apcTEncTop[0]->getVPS();

	vps.setMaxTLayers(m_apcLayerCfg[0]->m_maxTempLayer);
	if (m_apcLayerCfg[0]->m_maxTempLayer == 1)
#else
	TComVPS vps;

	vps.setMaxTLayers(m_maxTempLayer);
	if (m_maxTempLayer == 1)
#endif
	{
		vps.setTemporalNestingFlag(true);
	}
	vps.setMaxLayers(1);
	for (Int i = 0; i < MAX_TLAYER; i++)
	{
		vps.setNumReorderPics(m_numReorderPics[i], i);
		vps.setMaxDecPicBuffering(m_maxDecPicBuffering[i], i);
	}

#if !SVC_EXTENSION
	m_cTEncTop.setVPS(&vps);
#endif

#if SVC_EXTENSION
	vps.setVpsPocLsbAlignedFlag(false);

	Int maxRepFormatIdx = -1;
	Int formatIdx = -1;
	for (UInt layer = 0; layer < m_numLayers; layer++)
	{
		// Auto generation of the format index
		if (m_apcLayerCfg[layer]->getRepFormatIdx() == -1)
		{
			Bool found = false;
			for (UInt idx = 0; idx < layer; idx++)
			{
				if (m_apcLayerCfg[layer]->m_iSourceWidth == m_apcLayerCfg[idx]->m_iSourceWidth && m_apcLayerCfg[layer]->m_iSourceHeight == m_apcLayerCfg[idx]->m_iSourceHeight
#if AUXILIARY_PICTURES
					&& m_apcLayerCfg[layer]->m_chromaFormatIDC == m_apcLayerCfg[idx]->m_chromaFormatIDC
#endif
					&& m_apcLayerCfg[layer]->m_internalBitDepth[CHANNEL_TYPE_LUMA] == m_apcLayerCfg[idx]->m_internalBitDepth[CHANNEL_TYPE_LUMA] && m_apcLayerCfg[layer]->m_internalBitDepth[CHANNEL_TYPE_CHROMA] == m_apcLayerCfg[idx]->m_internalBitDepth[CHANNEL_TYPE_CHROMA]
					)
				{
					found = true;
					break;
				}
			}
			if (!found)
			{
				formatIdx++;
			}

			m_apcLayerCfg[layer]->setRepFormatIdx(formatIdx);
		}

		assert(m_apcLayerCfg[layer]->getRepFormatIdx() != -1 && "RepFormatIdx not assigned for a layer");

		vps.setVpsRepFormatIdx(layer, m_apcLayerCfg[layer]->getRepFormatIdx());

		maxRepFormatIdx = std::max(m_apcLayerCfg[layer]->getRepFormatIdx(), maxRepFormatIdx);

		for (Int compareLayer = 0; compareLayer < layer; compareLayer++)
		{
			if (m_apcLayerCfg[layer]->m_repFormatIdx == m_apcLayerCfg[compareLayer]->m_repFormatIdx && (
				m_apcLayerCfg[layer]->m_chromaFormatIDC != m_apcLayerCfg[compareLayer]->m_chromaFormatIDC
				// separate_colour_plane_flag not supported yet but if supported insert check here
				|| m_apcLayerCfg[layer]->m_iSourceWidth != m_apcLayerCfg[compareLayer]->m_iSourceWidth
				|| m_apcLayerCfg[layer]->m_iSourceHeight != m_apcLayerCfg[compareLayer]->m_iSourceHeight
				|| m_apcLayerCfg[layer]->m_internalBitDepth[0] != m_apcLayerCfg[compareLayer]->m_internalBitDepth[0]
				|| m_apcLayerCfg[layer]->m_internalBitDepth[1] != m_apcLayerCfg[compareLayer]->m_internalBitDepth[1]
				|| m_apcLayerCfg[layer]->m_confWinLeft != m_apcLayerCfg[compareLayer]->m_confWinLeft
				|| m_apcLayerCfg[layer]->m_confWinRight != m_apcLayerCfg[compareLayer]->m_confWinRight
				|| m_apcLayerCfg[layer]->m_confWinTop != m_apcLayerCfg[compareLayer]->m_confWinTop
				|| m_apcLayerCfg[layer]->m_confWinBottom != m_apcLayerCfg[compareLayer]->m_confWinBottom
				))
			{
				fprintf(stderr, "Error: Two layers using the same FormatIdx value must share the same values of the related parameters\n");
				exit(EXIT_FAILURE);
			}
		}
	}

	assert(vps.getVpsRepFormatIdx(0) == 0);  // Base layer should point to the first one.

	Int* mapIdxToLayer = new Int[maxRepFormatIdx + 1];

	// Check that all the indices from 0 to maxRepFormatIdx are used in the VPS
	for (Int i = 0; i <= maxRepFormatIdx; i++)
	{
		mapIdxToLayer[i] = -1;
		UInt layer;
		for (layer = 0; layer < m_numLayers; layer++)
		{
			if (vps.getVpsRepFormatIdx(layer) == i)
			{
				mapIdxToLayer[i] = layer;
				break;
			}
		}
		assert(layer != m_numLayers);   // One of the VPS Rep format indices not set
	}

	vps.setVpsNumRepFormats(maxRepFormatIdx + 1);

	// When not present, the value of rep_format_idx_present_flag is inferred to be equal to 0
	vps.setRepFormatIdxPresentFlag(vps.getVpsNumRepFormats() > 1 ? true : false);

	for (UInt idx = 0; idx < vps.getVpsNumRepFormats(); idx++)
	{
		RepFormat *repFormat = vps.getVpsRepFormat(idx);
		repFormat->setChromaAndBitDepthVpsPresentFlag(true);
		if (idx == 0)
		{
			assert(repFormat->getChromaAndBitDepthVpsPresentFlag() == true);
		}

		repFormat->setPicWidthVpsInLumaSamples(m_apcLayerCfg[mapIdxToLayer[idx]]->m_iSourceWidth);
		repFormat->setPicHeightVpsInLumaSamples(m_apcLayerCfg[mapIdxToLayer[idx]]->m_iSourceHeight);
#if AUXILIARY_PICTURES
		repFormat->setChromaFormatVpsIdc(m_apcLayerCfg[mapIdxToLayer[idx]]->m_chromaFormatIDC);
#else
		repFormat->setChromaFormatVpsIdc(1);  // Need modification to change for each layer - corresponds to 420
#endif
		repFormat->setSeparateColourPlaneVpsFlag(0);  // Need modification to change for each layer

		repFormat->setBitDepthVpsLuma(getInternalBitDepth(mapIdxToLayer[idx], CHANNEL_TYPE_LUMA));  // Need modification to change for each layer
		repFormat->setBitDepthVpsChroma(getInternalBitDepth(mapIdxToLayer[idx], CHANNEL_TYPE_CHROMA));  // Need modification to change for each layer

		repFormat->getConformanceWindowVps().setWindow(m_apcLayerCfg[mapIdxToLayer[idx]]->m_confWinLeft,
			m_apcLayerCfg[mapIdxToLayer[idx]]->m_confWinRight,
			m_apcLayerCfg[mapIdxToLayer[idx]]->m_confWinTop,
			m_apcLayerCfg[mapIdxToLayer[idx]]->m_confWinBottom);

		m_apcTEncTop[mapIdxToLayer[idx]]->setSkipPictureAtArcSwitch(m_skipPictureAtArcSwitch);
	}

	delete[] mapIdxToLayer;

	//Populate PTL in VPS
	for (Int ii = 0; ii < m_numPTLInfo; ii++)
	{
		vps.getPTL(ii)->getGeneralPTL()->setLevelIdc(m_levelList[ii]);
		vps.getPTL(ii)->getGeneralPTL()->setTierFlag(m_levelTierList[ii]);
		vps.getPTL(ii)->getGeneralPTL()->setProfileIdc(m_profileList[ii]);
		vps.getPTL(ii)->getGeneralPTL()->setProfileCompatibilityFlag(m_profileCompatibility[ii], 1);
		vps.getPTL(ii)->getGeneralPTL()->setProgressiveSourceFlag(m_progressiveSourceFlagList[ii]);
		vps.getPTL(ii)->getGeneralPTL()->setInterlacedSourceFlag(m_interlacedSourceFlagList[ii]);
		vps.getPTL(ii)->getGeneralPTL()->setNonPackedConstraintFlag(m_nonPackedConstraintFlagList[ii]);
		vps.getPTL(ii)->getGeneralPTL()->setFrameOnlyConstraintFlag(m_frameOnlyConstraintFlagList[ii]);
	}
	vps.setNumProfileTierLevel(m_numPTLInfo);

	std::vector<Int> myvector;
	vps.getProfileLevelTierIdx()->resize(m_numOutputLayerSets);
	for (Int ii = 0; ii < m_numOutputLayerSets; ii++)
	{
		myvector = m_listOfLayerPTLofOlss[ii];

		for (std::vector<Int>::iterator it = myvector.begin(); it != myvector.end(); ++it)
		{
			vps.addProfileLevelTierIdx(ii, it[0]);
		}
	}

	assert(m_numLayers <= MAX_LAYERS);

	for (UInt layer = 0; layer < m_numLayers; layer++)
	{
		TEncTop& m_cTEncTop = *m_apcTEncTop[layer];
		//1
		m_cTEncTop.setInterLayerWeightedPredFlag(m_useInterLayerWeightedPred);
#if AVC_BASE
		m_cTEncTop.setMFMEnabledFlag(layer == 0 ? false : (m_nonHEVCBaseLayerFlag ? false : true) && m_apcLayerCfg[layer]->getNumMotionPredRefLayers());
#else
		m_cTEncTop.setMFMEnabledFlag(layer == 0 ? false : m_apcLayerCfg[layer]->getNumMotionPredRefLayers());
#endif

		// set layer ID
		m_cTEncTop.setLayerId(m_apcLayerCfg[layer]->m_layerId);
		m_cTEncTop.setNumLayer(m_numLayers);
		m_cTEncTop.setLayerEnc(m_apcTEncTop);

		if (layer < m_numLayers - 1)
		{
			m_cTEncTop.setMaxTidIlRefPicsPlus1(m_apcLayerCfg[layer]->getMaxTidIlRefPicsPlus1());
		}

		if (layer)
		{
			UInt prevLayerIdx = 0;
			UInt prevLayerId = 0;
			if (m_apcLayerCfg[layer]->getNumActiveRefLayers() > 0)
			{
				prevLayerIdx = m_apcLayerCfg[layer]->getPredLayerIdx(m_apcLayerCfg[layer]->getNumActiveRefLayers() - 1);
				prevLayerId = m_cTEncTop.getRefLayerId(prevLayerIdx);
			}
			for (Int i = 0; i < MAX_VPS_LAYER_IDX_PLUS1; i++)
			{
				m_cTEncTop.setSamplePredEnabledFlag(i, false);
				m_cTEncTop.setMotionPredEnabledFlag(i, false);
			}
			if (m_apcLayerCfg[layer]->getNumSamplePredRefLayers() == -1)
			{
				// Not included in the configuration file; assume that each layer depends on previous layer
				m_cTEncTop.setNumSamplePredRefLayers(1);      // One sample pred ref. layer
				m_cTEncTop.setSamplePredRefLayerId(prevLayerIdx, prevLayerId);   // Previous layer
				m_cTEncTop.setSamplePredEnabledFlag(prevLayerIdx, true);
			}
			else
			{
				m_cTEncTop.setNumSamplePredRefLayers(m_apcLayerCfg[layer]->getNumSamplePredRefLayers());
				for (Int i = 0; i < m_cTEncTop.getNumSamplePredRefLayers(); i++)
				{
					m_cTEncTop.setSamplePredRefLayerId(i, m_apcLayerCfg[layer]->getSamplePredRefLayerId(i));
				}
			}
			if (m_apcLayerCfg[layer]->getNumMotionPredRefLayers() == -1)
			{
				// Not included in the configuration file; assume that each layer depends on previous layer
				m_cTEncTop.setNumMotionPredRefLayers(1);      // One motion pred ref. layer
				m_cTEncTop.setMotionPredRefLayerId(prevLayerIdx, prevLayerId);   // Previous layer
				m_cTEncTop.setMotionPredEnabledFlag(prevLayerIdx, true);
			}
			else
			{
				m_cTEncTop.setNumMotionPredRefLayers(m_apcLayerCfg[layer]->getNumMotionPredRefLayers());
				for (Int i = 0; i < m_cTEncTop.getNumMotionPredRefLayers(); i++)
				{
					m_cTEncTop.setMotionPredRefLayerId(i, m_apcLayerCfg[layer]->getMotionPredRefLayerId(i));
				}
			}
			Int numDirectRefLayers = 0;

			assert(layer < MAX_LAYERS);

			for (Int i = 0; i < m_apcLayerCfg[layer]->m_layerId; i++)
			{
				Int refLayerId = -1;

				for (Int layerIdc = 0; layerIdc < m_cTEncTop.getNumSamplePredRefLayers(); layerIdc++)
				{
					if (m_apcLayerCfg[layer]->getSamplePredRefLayerId(layerIdc) == i)
					{
						refLayerId = i;
						m_cTEncTop.setSamplePredEnabledFlag(numDirectRefLayers, true);
						break;
					}
				}

				for (Int layerIdc = 0; layerIdc < m_cTEncTop.getNumMotionPredRefLayers(); layerIdc++)
				{
					if (m_apcLayerCfg[layer]->getMotionPredRefLayerId(layerIdc) == i)
					{
						refLayerId = i;
						m_cTEncTop.setMotionPredEnabledFlag(numDirectRefLayers, true);
						break;
					}
				}

				if (refLayerId >= 0)
				{
					m_cTEncTop.setRefLayerId(numDirectRefLayers, refLayerId);
					numDirectRefLayers++;
				}
			}

			m_cTEncTop.setNumDirectRefLayers(numDirectRefLayers);

			if (m_apcLayerCfg[layer]->getNumActiveRefLayers() == -1)
			{
				m_cTEncTop.setNumActiveRefLayers(m_cTEncTop.getNumDirectRefLayers());
				for (Int i = 0; i < m_cTEncTop.getNumActiveRefLayers(); i++)
				{
					m_cTEncTop.setPredLayerIdx(i, i);
				}
			}
			else
			{
				m_cTEncTop.setNumActiveRefLayers(m_apcLayerCfg[layer]->getNumActiveRefLayers());
				for (Int i = 0; i < m_cTEncTop.getNumActiveRefLayers(); i++)
				{
					m_cTEncTop.setPredLayerIdx(i, m_apcLayerCfg[layer]->getPredLayerIdx(i));
				}
			}
		}
		else
		{
			assert(layer == 0);
			m_cTEncTop.setNumDirectRefLayers(0);
		}

		if (layer > 0)
		{
#if AUXILIARY_PICTURES
			Int subWidthC = (m_apcLayerCfg[layer]->m_chromaFormatIDC == CHROMA_420 || m_apcLayerCfg[layer]->m_chromaFormatIDC == CHROMA_422) ? 2 : 1;
			Int subHeightC = (m_apcLayerCfg[layer]->m_chromaFormatIDC == CHROMA_420) ? 2 : 1;
#else
			Int subWidthC = 2;
			Int subHeightC = 2;
#endif
			m_cTEncTop.setNumRefLayerLocationOffsets(m_apcLayerCfg[layer]->m_numRefLayerLocationOffsets);
			for (Int i = 0; i < m_apcLayerCfg[layer]->m_numRefLayerLocationOffsets; i++)
			{
				m_cTEncTop.setRefLocationOffsetLayerId(i, m_apcLayerCfg[layer]->m_refLocationOffsetLayerId[i]);
				m_cTEncTop.setScaledRefLayerOffsetPresentFlag(i, m_apcLayerCfg[layer]->m_scaledRefLayerOffsetPresentFlag[i]);
				m_cTEncTop.getScaledRefLayerWindow(i).setWindow(subWidthC  * m_apcLayerCfg[layer]->m_scaledRefLayerLeftOffset[i], subWidthC  * m_apcLayerCfg[layer]->m_scaledRefLayerRightOffset[i],
					subHeightC * m_apcLayerCfg[layer]->m_scaledRefLayerTopOffset[i], subHeightC * m_apcLayerCfg[layer]->m_scaledRefLayerBottomOffset[i]);

				Int rlSubWidthC = (m_apcLayerCfg[i]->m_chromaFormatIDC == CHROMA_420 || m_apcLayerCfg[i]->m_chromaFormatIDC == CHROMA_422) ? 2 : 1;
				Int rlSubHeightC = (m_apcLayerCfg[i]->m_chromaFormatIDC == CHROMA_420) ? 2 : 1;

				m_cTEncTop.setRefRegionOffsetPresentFlag(i, m_apcLayerCfg[layer]->m_refRegionOffsetPresentFlag[i]);
				m_cTEncTop.getRefLayerWindow(i).setWindow(rlSubWidthC  * m_apcLayerCfg[layer]->m_refRegionLeftOffset[i], rlSubWidthC  * m_apcLayerCfg[layer]->m_refRegionRightOffset[i],
					rlSubHeightC * m_apcLayerCfg[layer]->m_refRegionTopOffset[i], rlSubHeightC * m_apcLayerCfg[layer]->m_refRegionBottomOffset[i]);
				m_cTEncTop.setResamplePhaseSetPresentFlag(i, m_apcLayerCfg[layer]->m_resamplePhaseSetPresentFlag[i]);
				m_cTEncTop.setPhaseHorLuma(i, m_apcLayerCfg[layer]->m_phaseHorLuma[i]);
				m_cTEncTop.setPhaseVerLuma(i, m_apcLayerCfg[layer]->m_phaseVerLuma[i]);
				m_cTEncTop.setPhaseHorChroma(i, m_apcLayerCfg[layer]->m_phaseHorChroma[i]);
				m_cTEncTop.setPhaseVerChroma(i, m_apcLayerCfg[layer]->m_phaseVerChroma[i]);
			}
		}

		m_cTEncTop.setElRapSliceTypeB(m_elRapSliceBEnabled);

		m_cTEncTop.setAdaptiveResolutionChange(m_adaptiveResolutionChange);
		m_cTEncTop.setLayerSwitchOffBegin(m_apcLayerCfg[layer]->m_layerSwitchOffBegin);
		m_cTEncTop.setLayerSwitchOffEnd(m_apcLayerCfg[layer]->m_layerSwitchOffEnd);
		m_cTEncTop.setChromaFormatIDC(m_apcLayerCfg[layer]->m_chromaFormatIDC);
		m_cTEncTop.setAltOuputLayerFlag(m_altOutputLayerFlag);
		m_cTEncTop.setCrossLayerBLAFlag(m_crossLayerBLAFlag);
#if CGS_3D_ASYMLUT
		m_cTEncTop.setCGSFlag(layer == 0 ? 0 : m_nCGSFlag);
		m_cTEncTop.setCGSMaxOctantDepth(m_nCGSMaxOctantDepth);
		m_cTEncTop.setCGSMaxYPartNumLog2(m_nCGSMaxYPartNumLog2);
		m_cTEncTop.setCGSLUTBit(m_nCGSLUTBit);
		m_cTEncTop.setCGSAdaptChroma(m_nCGSAdaptiveChroma);
#if R0179_ENC_OPT_3DLUT_SIZE
		m_cTEncTop.setCGSLutSizeRDO(m_nCGSLutSizeRDO);
#endif
#endif
		m_cTEncTop.setNumAddLayerSets(m_numAddLayerSets);

#if FAST_INTRA_SHVC
		m_cTEncTop.setUseFastIntraScalable(m_useFastIntraScalable);
#endif

#if P0123_ALPHA_CHANNEL_SEI
		m_cTEncTop.setAlphaSEIEnabled(m_alphaSEIEnabled);
		m_cTEncTop.setAlphaCancelFlag(m_alphaCancelFlag);
		m_cTEncTop.setAlphaUseIdc(m_alphaUseIdc);
		m_cTEncTop.setAlphaBitDepthMinus8(m_alphaBitDepthMinus8);
		m_cTEncTop.setAlphaTransparentValue(m_alphaTransparentValue);
		m_cTEncTop.setAlphaOpaqueValue(m_alphaOpaqueValue);
		m_cTEncTop.setAlphaIncrementFlag(m_alphaIncrementFlag);
		m_cTEncTop.setAlphaClipFlag(m_alphaClipFlag);
		m_cTEncTop.setAlphaClipTypeFlag(m_alphaClipTypeFlag);
#endif
#if Q0096_OVERLAY_SEI
		m_cTEncTop.setOverlaySEIEnabled(m_overlaySEIEnabled);
		m_cTEncTop.setOverlaySEICancelFlag(m_overlayInfoCancelFlag);
		m_cTEncTop.setOverlaySEIContentAuxIdMinus128(m_overlayContentAuxIdMinus128);
		m_cTEncTop.setOverlaySEILabelAuxIdMinus128(m_overlayLabelAuxIdMinus128);
		m_cTEncTop.setOverlaySEIAlphaAuxIdMinus128(m_overlayAlphaAuxIdMinus128);
		m_cTEncTop.setOverlaySEIElementLabelValueLengthMinus8(m_overlayElementLabelValueLengthMinus8);
		m_cTEncTop.setOverlaySEINumOverlaysMinus1(m_numOverlaysMinus1);
		m_cTEncTop.setOverlaySEIIdx(m_overlayIdx);
		m_cTEncTop.setOverlaySEILanguagePresentFlag(m_overlayLanguagePresentFlag);
		m_cTEncTop.setOverlaySEIContentLayerId(m_overlayContentLayerId);
		m_cTEncTop.setOverlaySEILabelPresentFlag(m_overlayLabelPresentFlag);
		m_cTEncTop.setOverlaySEILabelLayerId(m_overlayLabelLayerId);
		m_cTEncTop.setOverlaySEIAlphaPresentFlag(m_overlayAlphaPresentFlag);
		m_cTEncTop.setOverlaySEIAlphaLayerId(m_overlayAlphaLayerId);
		m_cTEncTop.setOverlaySEINumElementsMinus1(m_numOverlayElementsMinus1);
		m_cTEncTop.setOverlaySEIElementLabelMin(m_overlayElementLabelMin);
		m_cTEncTop.setOverlaySEIElementLabelMax(m_overlayElementLabelMax);
		m_cTEncTop.setOverlaySEILanguage(m_overlayLanguage);
		m_cTEncTop.setOverlaySEIName(m_overlayName);
		m_cTEncTop.setOverlaySEIElementName(m_overlayElementName);
		m_cTEncTop.setOverlaySEIPersistenceFlag(m_overlayInfoPersistenceFlag);
#endif  
#if Q0189_TMVP_CONSTRAINTS
		m_cTEncTop.setTMVPConstraintsSEIEnabled(m_TMVPConstraintsSEIEnabled);
#endif
#if N0383_IL_CONSTRAINED_TILE_SETS_SEI
		m_cTEncTop.setInterLayerConstrainedTileSetsSEIEnabled(m_interLayerConstrainedTileSetsSEIEnabled);
		m_cTEncTop.setIlNumSetsInMessage(m_ilNumSetsInMessage);
		m_cTEncTop.setSkippedTileSetPresentFlag(m_skippedTileSetPresentFlag);
		m_cTEncTop.setTopLeftTileIndex(m_topLeftTileIndex);
		m_cTEncTop.setBottomRightTileIndex(m_bottomRightTileIndex);
		m_cTEncTop.setIlcIdc(m_ilcIdc);
#endif
#if VIEW_SCALABILITY 
		m_cTEncTop.setUseDisparitySearchRangeRestriction(m_scalabilityMask[VIEW_ORDER_INDEX]);
#endif

		Int& layerPTLIdx = m_apcLayerCfg[layer]->m_layerPTLIdx;

		Profile::Name& m_profile = m_profileList[layerPTLIdx];
		Level::Tier&   m_levelTier = m_levelTierList[layerPTLIdx];
		Level::Name&   m_level = m_levelList[layerPTLIdx];
		UInt&          m_bitDepthConstraint = m_apcLayerCfg[layer]->m_bitDepthConstraint;
		ChromaFormat&  m_chromaFormatConstraint = m_apcLayerCfg[layer]->m_chromaFormatConstraint;
		Bool&          m_intraConstraintFlag = m_apcLayerCfg[layer]->m_intraConstraintFlag;
		Bool&          m_onePictureOnlyConstraintFlag = m_apcLayerCfg[layer]->m_onePictureOnlyConstraintFlag;
		Bool&          m_lowerBitRateConstraintFlag = m_apcLayerCfg[layer]->m_lowerBitRateConstraintFlag;
		Bool&          m_progressiveSourceFlag = m_progressiveSourceFlagList[layerPTLIdx];
		Bool&          m_interlacedSourceFlag = m_interlacedSourceFlagList[layerPTLIdx];
		Bool&          m_nonPackedConstraintFlag = m_nonPackedConstraintFlagList[layerPTLIdx];
		Bool&          m_frameOnlyConstraintFlag = m_frameOnlyConstraintFlagList[layerPTLIdx];

		Int&           m_iFrameRate = m_apcLayerCfg[layer]->m_iFrameRate;
		Int&           m_iSourceWidth = m_apcLayerCfg[layer]->m_iSourceWidth;
		Int&           m_iSourceHeight = m_apcLayerCfg[layer]->m_iSourceHeight;
		Int&           m_confWinLeft = m_apcLayerCfg[layer]->m_confWinLeft;
		Int&           m_confWinRight = m_apcLayerCfg[layer]->m_confWinRight;
		Int&           m_confWinTop = m_apcLayerCfg[layer]->m_confWinTop;
		Int&           m_confWinBottom = m_apcLayerCfg[layer]->m_confWinBottom;

		Int&           m_iIntraPeriod = m_apcLayerCfg[layer]->m_iIntraPeriod;
		Int&           m_iQP = m_apcLayerCfg[layer]->m_iQP;
		Int           *m_aiPad = m_apcLayerCfg[layer]->m_aiPad;

		Int&           m_iMaxCuDQPDepth = m_apcLayerCfg[layer]->m_iMaxCuDQPDepth;
		ChromaFormat&  m_chromaFormatIDC = m_apcLayerCfg[layer]->m_chromaFormatIDC;
		Int           *m_aidQP = m_apcLayerCfg[layer]->m_aidQP;

		UInt&          m_uiMaxCUWidth = m_apcLayerCfg[layer]->m_uiMaxCUWidth;
		UInt&          m_uiMaxCUHeight = m_apcLayerCfg[layer]->m_uiMaxCUHeight;
		UInt&          m_uiMaxTotalCUDepth = m_apcLayerCfg[layer]->m_uiMaxTotalCUDepth;
		UInt&          m_uiLog2DiffMaxMinCodingBlockSize = m_apcLayerCfg[layer]->m_uiLog2DiffMaxMinCodingBlockSize;
		UInt&          m_uiQuadtreeTULog2MaxSize = m_apcLayerCfg[layer]->m_uiQuadtreeTULog2MaxSize;
		UInt&          m_uiQuadtreeTULog2MinSize = m_apcLayerCfg[layer]->m_uiQuadtreeTULog2MinSize;
		UInt&          m_uiQuadtreeTUMaxDepthInter = m_apcLayerCfg[layer]->m_uiQuadtreeTUMaxDepthInter;
		UInt&          m_uiQuadtreeTUMaxDepthIntra = m_apcLayerCfg[layer]->m_uiQuadtreeTUMaxDepthIntra;

		Bool&          m_entropyCodingSyncEnabledFlag = m_apcLayerCfg[layer]->m_entropyCodingSyncEnabledFlag;
		Bool&          m_RCEnableRateControl = m_apcLayerCfg[layer]->m_RCEnableRateControl;
		Int&           m_RCTargetBitrate = m_apcLayerCfg[layer]->m_RCTargetBitrate;
		Bool&          m_RCKeepHierarchicalBit = m_apcLayerCfg[layer]->m_RCKeepHierarchicalBit;
		Bool&          m_RCLCULevelRC = m_apcLayerCfg[layer]->m_RCLCULevelRC;
		Bool&          m_RCUseLCUSeparateModel = m_apcLayerCfg[layer]->m_RCUseLCUSeparateModel;
		Int&           m_RCInitialQP = m_apcLayerCfg[layer]->m_RCInitialQP;
		Bool&          m_RCForceIntraQP = m_apcLayerCfg[layer]->m_RCForceIntraQP;

#if U0132_TARGET_BITS_SATURATION
		Bool&          m_RCCpbSaturationEnabled = m_apcLayerCfg[layer]->m_RCCpbSaturationEnabled;
		UInt&          m_RCCpbSize = m_apcLayerCfg[layer]->m_RCCpbSize;
		Double&        m_RCInitialCpbFullness = m_apcLayerCfg[layer]->m_RCInitialCpbFullness;
#endif

		ScalingListMode& m_useScalingListId = m_apcLayerCfg[layer]->m_useScalingListId;
		string&        m_scalingListFileName = m_apcLayerCfg[layer]->m_scalingListFileName;
		Bool&          m_bUseSAO = m_apcLayerCfg[layer]->m_bUseSAO;

#if Machine_Learning_Debug
		string&        m_saliencyFileName = m_apcLayerCfg[layer]->m_saliencyFileName;
#endif

		GOPEntry*      m_GOPList = m_apcLayerCfg[layer]->m_GOPList;
		Int&           m_extraRPSs = m_apcLayerCfg[layer]->m_extraRPSs;
		Int&           m_maxTempLayer = m_apcLayerCfg[layer]->m_maxTempLayer;

		string&        m_colourRemapSEIFileRoot = m_apcLayerCfg[layer]->m_colourRemapSEIFileRoot;
#if PER_LAYER_LOSSLESS
		Bool&          m_TransquantBypassEnabledFlag = m_apcLayerCfg[layer]->m_TransquantBypassEnabledFlag;
		Bool&          m_CUTransquantBypassFlagForce = m_apcLayerCfg[layer]->m_CUTransquantBypassFlagForce;
		CostMode&      m_costMode = m_apcLayerCfg[layer]->m_costMode;
#endif
#endif

		m_cTEncTop.setProfile(m_profile);
		m_cTEncTop.setLevel(m_levelTier, m_level);
		m_cTEncTop.setProgressiveSourceFlag(m_progressiveSourceFlag);
		m_cTEncTop.setInterlacedSourceFlag(m_interlacedSourceFlag);
		m_cTEncTop.setNonPackedConstraintFlag(m_nonPackedConstraintFlag);
		m_cTEncTop.setFrameOnlyConstraintFlag(m_frameOnlyConstraintFlag);
		m_cTEncTop.setBitDepthConstraintValue(m_bitDepthConstraint);
		m_cTEncTop.setChromaFormatConstraintValue(m_chromaFormatConstraint);
		m_cTEncTop.setIntraConstraintFlag(m_intraConstraintFlag);
		m_cTEncTop.setOnePictureOnlyConstraintFlag(m_onePictureOnlyConstraintFlag);
		m_cTEncTop.setLowerBitRateConstraintFlag(m_lowerBitRateConstraintFlag);

		m_cTEncTop.setPrintMSEBasedSequencePSNR(m_printMSEBasedSequencePSNR);
		m_cTEncTop.setPrintFrameMSE(m_printFrameMSE);
		m_cTEncTop.setPrintSequenceMSE(m_printSequenceMSE);
		m_cTEncTop.setCabacZeroWordPaddingEnabled(m_cabacZeroWordPaddingEnabled);

		m_cTEncTop.setFrameRate(m_iFrameRate);
		m_cTEncTop.setFrameSkip(m_FrameSkip);
		m_cTEncTop.setTemporalSubsampleRatio(m_temporalSubsampleRatio);
		m_cTEncTop.setSourceWidth(m_iSourceWidth);
		m_cTEncTop.setSourceHeight(m_iSourceHeight);
		m_cTEncTop.setConformanceWindow(m_confWinLeft, m_confWinRight, m_confWinTop, m_confWinBottom);
		m_cTEncTop.setFramesToBeEncoded(m_framesToBeEncoded);

#if Machine_Learning_Debug
		m_cTEncTop.setLevel0(CU_OPTIMIZATION_LEVEL0);
		m_cTEncTop.setLevel1(CU_OPTIMIZATION_LEVEL1);
		m_cTEncTop.setLevel2(CU_OPTIMIZATION_LEVEL2);

		m_cTEncTop.setTP0(threshold_prob_0_positivie);
		m_cTEncTop.setTP1(threshold_prob_1_positivie);
		m_cTEncTop.setTP2(threshold_prob_2_positivie);

		m_cTEncTop.setTN0(threshold_prob_0_negative);
		m_cTEncTop.setTN1(threshold_prob_1_negative);
		m_cTEncTop.setTN2(threshold_prob_2_negative);//LXY
#endif

  //====== Coding Structure ========
		m_cTEncTop.setIntraPeriod(m_iIntraPeriod);
		m_cTEncTop.setDecodingRefreshType(m_iDecodingRefreshType);
		m_cTEncTop.setGOPSize(m_iGOPSize);
		m_cTEncTop.setGopList(m_GOPList);
		m_cTEncTop.setExtraRPSs(m_extraRPSs);
		for (Int i = 0; i < MAX_TLAYER; i++)
		{
			m_cTEncTop.setNumReorderPics(m_numReorderPics[i], i);
			m_cTEncTop.setMaxDecPicBuffering(m_maxDecPicBuffering[i], i);
		}
		for (UInt uiLoop = 0; uiLoop < MAX_TLAYER; ++uiLoop)
		{
			m_cTEncTop.setLambdaModifier(uiLoop, m_adLambdaModifier[uiLoop]);
		}
		m_cTEncTop.setIntraLambdaModifier(m_adIntraLambdaModifier);
		m_cTEncTop.setIntraQpFactor(m_dIntraQpFactor);

		m_cTEncTop.setQP(m_iQP);

		m_cTEncTop.setPad(m_aiPad);

		m_cTEncTop.setAccessUnitDelimiter(m_AccessUnitDelimiter);

		m_cTEncTop.setMaxTempLayer(m_maxTempLayer);
		m_cTEncTop.setUseAMP(m_enableAMP);

		//===== Slice ========

		//====== Loop/Deblock Filter ========
		m_cTEncTop.setLoopFilterDisable(m_bLoopFilterDisable);
		m_cTEncTop.setLoopFilterOffsetInPPS(m_loopFilterOffsetInPPS);
		m_cTEncTop.setLoopFilterBetaOffset(m_loopFilterBetaOffsetDiv2);
		m_cTEncTop.setLoopFilterTcOffset(m_loopFilterTcOffsetDiv2);
#if W0038_DB_OPT
		m_cTEncTop.setDeblockingFilterMetric(m_deblockingFilterMetric);
#else
		m_cTEncTop.setDeblockingFilterMetric(m_DeblockingFilterMetric);
#endif

		//====== Motion search ========
		m_cTEncTop.setDisableIntraPUsInInterSlices(m_bDisableIntraPUsInInterSlices);
		m_cTEncTop.setMotionEstimationSearchMethod(m_motionEstimationSearchMethod);
		m_cTEncTop.setSearchRange(m_iSearchRange);
		m_cTEncTop.setBipredSearchRange(m_bipredSearchRange);
		m_cTEncTop.setClipForBiPredMeEnabled(m_bClipForBiPredMeEnabled);
		m_cTEncTop.setFastMEAssumingSmootherMVEnabled(m_bFastMEAssumingSmootherMVEnabled);
		m_cTEncTop.setMinSearchWindow(m_minSearchWindow);
		m_cTEncTop.setRestrictMESampling(m_bRestrictMESampling);

		//====== Quality control ========
		m_cTEncTop.setMaxDeltaQP(m_iMaxDeltaQP);
		m_cTEncTop.setMaxCuDQPDepth(m_iMaxCuDQPDepth);
		m_cTEncTop.setDiffCuChromaQpOffsetDepth(m_diffCuChromaQpOffsetDepth);
		m_cTEncTop.setChromaCbQpOffset(m_cbQpOffset);
		m_cTEncTop.setChromaCrQpOffset(m_crQpOffset);
#if W0038_CQP_ADJ
		m_cTEncTop.setSliceChromaOffsetQpIntraOrPeriodic(m_sliceChromaQpOffsetPeriodicity, m_sliceChromaQpOffsetIntraOrPeriodic);
#endif
		m_cTEncTop.setChromaFormatIdc(m_chromaFormatIDC);

#if ADAPTIVE_QP_SELECTION
		m_cTEncTop.setUseAdaptQpSelect(m_bUseAdaptQpSelect);
#endif

		m_cTEncTop.setUseAdaptiveQP(m_bUseAdaptiveQP);
		m_cTEncTop.setQPAdaptationRange(m_iQPAdaptationRange);
		m_cTEncTop.setExtendedPrecisionProcessingFlag(m_extendedPrecisionProcessingFlag);
		m_cTEncTop.setHighPrecisionOffsetsEnabledFlag(m_highPrecisionOffsetsEnabledFlag);

		m_cTEncTop.setWeightedPredictionMethod(m_weightedPredictionMethod);

		//====== Tool list ========
		m_cTEncTop.setDeltaQpRD(m_uiDeltaQpRD);
		m_cTEncTop.setFastDeltaQp(m_bFastDeltaQP);
		m_cTEncTop.setUseASR(m_bUseASR);
		m_cTEncTop.setUseHADME(m_bUseHADME);
		m_cTEncTop.setdQPs(m_aidQP);
		m_cTEncTop.setUseRDOQ(m_useRDOQ);
		m_cTEncTop.setUseRDOQTS(m_useRDOQTS);
#if T0196_SELECTIVE_RDOQ
		m_cTEncTop.setUseSelectiveRDOQ(m_useSelectiveRDOQ);
#endif
		m_cTEncTop.setRDpenalty(m_rdPenalty);
		m_cTEncTop.setMaxCUWidth(m_uiMaxCUWidth);
		m_cTEncTop.setMaxCUHeight(m_uiMaxCUHeight);
		m_cTEncTop.setMaxTotalCUDepth(m_uiMaxTotalCUDepth);
		m_cTEncTop.setLog2DiffMaxMinCodingBlockSize(m_uiLog2DiffMaxMinCodingBlockSize);
		m_cTEncTop.setQuadtreeTULog2MaxSize(m_uiQuadtreeTULog2MaxSize);
		m_cTEncTop.setQuadtreeTULog2MinSize(m_uiQuadtreeTULog2MinSize);
		m_cTEncTop.setQuadtreeTUMaxDepthInter(m_uiQuadtreeTUMaxDepthInter);
		m_cTEncTop.setQuadtreeTUMaxDepthIntra(m_uiQuadtreeTUMaxDepthIntra);
		m_cTEncTop.setFastInterSearchMode(m_fastInterSearchMode);
		m_cTEncTop.setUseEarlyCU(m_bUseEarlyCU);
		m_cTEncTop.setUseFastDecisionForMerge(m_useFastDecisionForMerge);
		m_cTEncTop.setUseCbfFastMode(m_bUseCbfFastMode);
		m_cTEncTop.setUseEarlySkipDetection(m_useEarlySkipDetection);
		m_cTEncTop.setCrossComponentPredictionEnabledFlag(m_crossComponentPredictionEnabledFlag);
		m_cTEncTop.setUseReconBasedCrossCPredictionEstimate(m_reconBasedCrossCPredictionEstimate);
		m_cTEncTop.setLog2SaoOffsetScale(CHANNEL_TYPE_LUMA, m_log2SaoOffsetScale[CHANNEL_TYPE_LUMA]);
		m_cTEncTop.setLog2SaoOffsetScale(CHANNEL_TYPE_CHROMA, m_log2SaoOffsetScale[CHANNEL_TYPE_CHROMA]);
		m_cTEncTop.setUseTransformSkip(m_useTransformSkip);
		m_cTEncTop.setUseTransformSkipFast(m_useTransformSkipFast);
		m_cTEncTop.setTransformSkipRotationEnabledFlag(m_transformSkipRotationEnabledFlag);
		m_cTEncTop.setTransformSkipContextEnabledFlag(m_transformSkipContextEnabledFlag);
		m_cTEncTop.setPersistentRiceAdaptationEnabledFlag(m_persistentRiceAdaptationEnabledFlag);
		m_cTEncTop.setCabacBypassAlignmentEnabledFlag(m_cabacBypassAlignmentEnabledFlag);
		m_cTEncTop.setLog2MaxTransformSkipBlockSize(m_log2MaxTransformSkipBlockSize);
		for (UInt signallingModeIndex = 0; signallingModeIndex < NUMBER_OF_RDPCM_SIGNALLING_MODES; signallingModeIndex++)
		{
			m_cTEncTop.setRdpcmEnabledFlag(RDPCMSignallingMode(signallingModeIndex), m_rdpcmEnabledFlag[signallingModeIndex]);
		}
		m_cTEncTop.setUseConstrainedIntraPred(m_bUseConstrainedIntraPred);
		m_cTEncTop.setFastUDIUseMPMEnabled(m_bFastUDIUseMPMEnabled);
		m_cTEncTop.setFastMEForGenBLowDelayEnabled(m_bFastMEForGenBLowDelayEnabled);
		m_cTEncTop.setUseBLambdaForNonKeyLowDelayPictures(m_bUseBLambdaForNonKeyLowDelayPictures);
		m_cTEncTop.setPCMLog2MinSize(m_uiPCMLog2MinSize);
		m_cTEncTop.setUsePCM(m_usePCM);

		// set internal bit-depth and constants
		for (UInt channelType = 0; channelType < MAX_NUM_CHANNEL_TYPE; channelType++)
		{
#if SVC_EXTENSION
			m_cTEncTop.setBitDepth((ChannelType)channelType, m_apcLayerCfg[layer]->m_internalBitDepth[channelType]);
			m_cTEncTop.setPCMBitDepth((ChannelType)channelType, m_bPCMInputBitDepthFlag ? m_apcLayerCfg[layer]->m_MSBExtendedBitDepth[channelType] : m_apcLayerCfg[layer]->m_internalBitDepth[channelType]);
#else
			m_cTEncTop.setBitDepth((ChannelType)channelType, m_internalBitDepth[channelType]);
			m_cTEncTop.setPCMBitDepth((ChannelType)channelType, m_bPCMInputBitDepthFlag ? m_MSBExtendedBitDepth[channelType] : m_internalBitDepth[channelType]);
#endif
		}

		m_cTEncTop.setPCMLog2MaxSize(m_pcmLog2MaxSize);
		m_cTEncTop.setMaxNumMergeCand(m_maxNumMergeCand);


		//====== Weighted Prediction ========
		m_cTEncTop.setUseWP(m_useWeightedPred);
		m_cTEncTop.setWPBiPred(m_useWeightedBiPred);

		//====== Parallel Merge Estimation ========
		m_cTEncTop.setLog2ParallelMergeLevelMinus2(m_log2ParallelMergeLevel - 2);

		//====== Slice ========
		m_cTEncTop.setSliceMode(m_sliceMode);
		m_cTEncTop.setSliceArgument(m_sliceArgument);

		//====== Dependent Slice ========
		m_cTEncTop.setSliceSegmentMode(m_sliceSegmentMode);
		m_cTEncTop.setSliceSegmentArgument(m_sliceSegmentArgument);

		if (m_sliceMode == NO_SLICES)
		{
			m_bLFCrossSliceBoundaryFlag = true;
		}
		m_cTEncTop.setLFCrossSliceBoundaryFlag(m_bLFCrossSliceBoundaryFlag);
		m_cTEncTop.setUseSAO(m_bUseSAO);
		m_cTEncTop.setTestSAODisableAtPictureLevel(m_bTestSAODisableAtPictureLevel);
		m_cTEncTop.setSaoEncodingRate(m_saoEncodingRate);
		m_cTEncTop.setSaoEncodingRateChroma(m_saoEncodingRateChroma);
		m_cTEncTop.setMaxNumOffsetsPerPic(m_maxNumOffsetsPerPic);

		m_cTEncTop.setSaoCtuBoundary(m_saoCtuBoundary);
#if OPTIONAL_RESET_SAO_ENCODING_AFTER_IRAP
		m_cTEncTop.setSaoResetEncoderStateAfterIRAP(m_saoResetEncoderStateAfterIRAP);
#endif
		m_cTEncTop.setPCMInputBitDepthFlag(m_bPCMInputBitDepthFlag);
		m_cTEncTop.setPCMFilterDisableFlag(m_bPCMFilterDisableFlag);

		m_cTEncTop.setIntraSmoothingDisabledFlag(!m_enableIntraReferenceSmoothing);
		m_cTEncTop.setDecodedPictureHashSEIType(m_decodedPictureHashSEIType);
		m_cTEncTop.setRecoveryPointSEIEnabled(m_recoveryPointSEIEnabled);
		m_cTEncTop.setBufferingPeriodSEIEnabled(m_bufferingPeriodSEIEnabled);
		m_cTEncTop.setPictureTimingSEIEnabled(m_pictureTimingSEIEnabled);
		m_cTEncTop.setToneMappingInfoSEIEnabled(m_toneMappingInfoSEIEnabled);
		m_cTEncTop.setTMISEIToneMapId(m_toneMapId);
		m_cTEncTop.setTMISEIToneMapCancelFlag(m_toneMapCancelFlag);
		m_cTEncTop.setTMISEIToneMapPersistenceFlag(m_toneMapPersistenceFlag);
		m_cTEncTop.setTMISEICodedDataBitDepth(m_toneMapCodedDataBitDepth);
		m_cTEncTop.setTMISEITargetBitDepth(m_toneMapTargetBitDepth);
		m_cTEncTop.setTMISEIModelID(m_toneMapModelId);
		m_cTEncTop.setTMISEIMinValue(m_toneMapMinValue);
		m_cTEncTop.setTMISEIMaxValue(m_toneMapMaxValue);
		m_cTEncTop.setTMISEISigmoidMidpoint(m_sigmoidMidpoint);
		m_cTEncTop.setTMISEISigmoidWidth(m_sigmoidWidth);
		m_cTEncTop.setTMISEIStartOfCodedInterva(m_startOfCodedInterval);
		m_cTEncTop.setTMISEINumPivots(m_numPivots);
		m_cTEncTop.setTMISEICodedPivotValue(m_codedPivotValue);
		m_cTEncTop.setTMISEITargetPivotValue(m_targetPivotValue);
		m_cTEncTop.setTMISEICameraIsoSpeedIdc(m_cameraIsoSpeedIdc);
		m_cTEncTop.setTMISEICameraIsoSpeedValue(m_cameraIsoSpeedValue);
		m_cTEncTop.setTMISEIExposureIndexIdc(m_exposureIndexIdc);
		m_cTEncTop.setTMISEIExposureIndexValue(m_exposureIndexValue);
		m_cTEncTop.setTMISEIExposureCompensationValueSignFlag(m_exposureCompensationValueSignFlag);
		m_cTEncTop.setTMISEIExposureCompensationValueNumerator(m_exposureCompensationValueNumerator);
		m_cTEncTop.setTMISEIExposureCompensationValueDenomIdc(m_exposureCompensationValueDenomIdc);
		m_cTEncTop.setTMISEIRefScreenLuminanceWhite(m_refScreenLuminanceWhite);
		m_cTEncTop.setTMISEIExtendedRangeWhiteLevel(m_extendedRangeWhiteLevel);
		m_cTEncTop.setTMISEINominalBlackLevelLumaCodeValue(m_nominalBlackLevelLumaCodeValue);
		m_cTEncTop.setTMISEINominalWhiteLevelLumaCodeValue(m_nominalWhiteLevelLumaCodeValue);
		m_cTEncTop.setTMISEIExtendedWhiteLevelLumaCodeValue(m_extendedWhiteLevelLumaCodeValue);
		m_cTEncTop.setChromaResamplingFilterHintEnabled(m_chromaResamplingFilterSEIenabled);
		m_cTEncTop.setChromaResamplingHorFilterIdc(m_chromaResamplingHorFilterIdc);
		m_cTEncTop.setChromaResamplingVerFilterIdc(m_chromaResamplingVerFilterIdc);
		m_cTEncTop.setFramePackingArrangementSEIEnabled(m_framePackingSEIEnabled);
		m_cTEncTop.setFramePackingArrangementSEIType(m_framePackingSEIType);
		m_cTEncTop.setFramePackingArrangementSEIId(m_framePackingSEIId);
		m_cTEncTop.setFramePackingArrangementSEIQuincunx(m_framePackingSEIQuincunx);
		m_cTEncTop.setFramePackingArrangementSEIInterpretation(m_framePackingSEIInterpretation);
		m_cTEncTop.setSegmentedRectFramePackingArrangementSEIEnabled(m_segmentedRectFramePackingSEIEnabled);
		m_cTEncTop.setSegmentedRectFramePackingArrangementSEICancel(m_segmentedRectFramePackingSEICancel);
		m_cTEncTop.setSegmentedRectFramePackingArrangementSEIType(m_segmentedRectFramePackingSEIType);
		m_cTEncTop.setSegmentedRectFramePackingArrangementSEIPersistence(m_segmentedRectFramePackingSEIPersistence);
		m_cTEncTop.setDisplayOrientationSEIAngle(m_displayOrientationSEIAngle);
		m_cTEncTop.setTemporalLevel0IndexSEIEnabled(m_temporalLevel0IndexSEIEnabled);
		m_cTEncTop.setGradualDecodingRefreshInfoEnabled(m_gradualDecodingRefreshInfoEnabled);
		m_cTEncTop.setNoDisplaySEITLayer(m_noDisplaySEITLayer);
		m_cTEncTop.setDecodingUnitInfoSEIEnabled(m_decodingUnitInfoSEIEnabled);
		m_cTEncTop.setSOPDescriptionSEIEnabled(m_SOPDescriptionSEIEnabled);
		m_cTEncTop.setScalableNestingSEIEnabled(m_scalableNestingSEIEnabled);
		m_cTEncTop.setTMCTSSEIEnabled(m_tmctsSEIEnabled);
		m_cTEncTop.setTimeCodeSEIEnabled(m_timeCodeSEIEnabled);
		m_cTEncTop.setNumberOfTimeSets(m_timeCodeSEINumTs);
		for (Int i = 0; i < m_timeCodeSEINumTs; i++)
		{
			m_cTEncTop.setTimeSet(m_timeSetArray[i], i);
		}
		m_cTEncTop.setKneeSEIEnabled(m_kneeSEIEnabled);
		m_cTEncTop.setKneeSEIId(m_kneeSEIId);
		m_cTEncTop.setKneeSEICancelFlag(m_kneeSEICancelFlag);
		m_cTEncTop.setKneeSEIPersistenceFlag(m_kneeSEIPersistenceFlag);
		m_cTEncTop.setKneeSEIInputDrange(m_kneeSEIInputDrange);
		m_cTEncTop.setKneeSEIInputDispLuminance(m_kneeSEIInputDispLuminance);
		m_cTEncTop.setKneeSEIOutputDrange(m_kneeSEIOutputDrange);
		m_cTEncTop.setKneeSEIOutputDispLuminance(m_kneeSEIOutputDispLuminance);
		m_cTEncTop.setKneeSEINumKneePointsMinus1(m_kneeSEINumKneePointsMinus1);
		m_cTEncTop.setKneeSEIInputKneePoint(m_kneeSEIInputKneePoint);
		m_cTEncTop.setKneeSEIOutputKneePoint(m_kneeSEIOutputKneePoint);
		m_cTEncTop.setColourRemapInfoSEIFileRoot(m_colourRemapSEIFileRoot);
		m_cTEncTop.setMasteringDisplaySEI(m_masteringDisplay);
#if U0033_ALTERNATIVE_TRANSFER_CHARACTERISTICS_SEI
		m_cTEncTop.setSEIAlternativeTransferCharacteristicsSEIEnable(m_preferredTransferCharacteristics >= 0);
		m_cTEncTop.setSEIPreferredTransferCharacteristics(UChar(m_preferredTransferCharacteristics));
#endif
		m_cTEncTop.setSEIGreenMetadataInfoSEIEnable(m_greenMetadataType > 0);
		m_cTEncTop.setSEIGreenMetadataType(UChar(m_greenMetadataType));
		m_cTEncTop.setSEIXSDMetricType(UChar(m_xsdMetricType));

		m_cTEncTop.setTileUniformSpacingFlag(m_tileUniformSpacingFlag);
		m_cTEncTop.setNumColumnsMinus1(m_numTileColumnsMinus1);
		m_cTEncTop.setNumRowsMinus1(m_numTileRowsMinus1);
		if (!m_tileUniformSpacingFlag)
		{
			m_cTEncTop.setColumnWidth(m_tileColumnWidth);
			m_cTEncTop.setRowHeight(m_tileRowHeight);
		}
		m_cTEncTop.xCheckGSParameters();
		Int uiTilesCount = (m_numTileRowsMinus1 + 1) * (m_numTileColumnsMinus1 + 1);
		if (uiTilesCount == 1)
		{
			m_bLFCrossTileBoundaryFlag = true;
		}
		m_cTEncTop.setLFCrossTileBoundaryFlag(m_bLFCrossTileBoundaryFlag);
		m_cTEncTop.setEntropyCodingSyncEnabledFlag(m_entropyCodingSyncEnabledFlag);
		m_cTEncTop.setTMVPModeId(m_TMVPModeId);
		m_cTEncTop.setUseScalingListId(m_useScalingListId);
		m_cTEncTop.setScalingListFileName(m_scalingListFileName);
#if Machine_Learning_Debug
		m_cTEncTop.setSaliencyFile(m_saliencyFileName);//LXY
#endif
		m_cTEncTop.setSignDataHidingEnabledFlag(m_signDataHidingEnabledFlag);
		m_cTEncTop.setUseRateCtrl(m_RCEnableRateControl);
		m_cTEncTop.setTargetBitrate(m_RCTargetBitrate);
		m_cTEncTop.setKeepHierBit(m_RCKeepHierarchicalBit);
		m_cTEncTop.setLCULevelRC(m_RCLCULevelRC);
		m_cTEncTop.setUseLCUSeparateModel(m_RCUseLCUSeparateModel);
		m_cTEncTop.setInitialQP(m_RCInitialQP);
		m_cTEncTop.setForceIntraQP(m_RCForceIntraQP);
#if U0132_TARGET_BITS_SATURATION
		m_cTEncTop.setCpbSaturationEnabled(m_RCCpbSaturationEnabled);
		m_cTEncTop.setCpbSize(m_RCCpbSize);
		m_cTEncTop.setInitialCpbFullness(m_RCInitialCpbFullness);
#endif
		m_cTEncTop.setTransquantBypassEnabledFlag(m_TransquantBypassEnabledFlag);
		m_cTEncTop.setCUTransquantBypassFlagForceValue(m_CUTransquantBypassFlagForce);
		m_cTEncTop.setCostMode(m_costMode);
		m_cTEncTop.setUseRecalculateQPAccordingToLambda(m_recalculateQPAccordingToLambda);
		m_cTEncTop.setUseStrongIntraSmoothing(m_useStrongIntraSmoothing);
		m_cTEncTop.setActiveParameterSetsSEIEnabled(m_activeParameterSetsSEIEnabled);
		m_cTEncTop.setVuiParametersPresentFlag(m_vuiParametersPresentFlag);
		m_cTEncTop.setAspectRatioInfoPresentFlag(m_aspectRatioInfoPresentFlag);
		m_cTEncTop.setAspectRatioIdc(m_aspectRatioIdc);
		m_cTEncTop.setSarWidth(m_sarWidth);
		m_cTEncTop.setSarHeight(m_sarHeight);
		m_cTEncTop.setOverscanInfoPresentFlag(m_overscanInfoPresentFlag);
		m_cTEncTop.setOverscanAppropriateFlag(m_overscanAppropriateFlag);
		m_cTEncTop.setVideoSignalTypePresentFlag(m_videoSignalTypePresentFlag);
		m_cTEncTop.setVideoFormat(m_videoFormat);
		m_cTEncTop.setVideoFullRangeFlag(m_videoFullRangeFlag);
		m_cTEncTop.setColourDescriptionPresentFlag(m_colourDescriptionPresentFlag);
		m_cTEncTop.setColourPrimaries(m_colourPrimaries);
		m_cTEncTop.setTransferCharacteristics(m_transferCharacteristics);
		m_cTEncTop.setMatrixCoefficients(m_matrixCoefficients);
		m_cTEncTop.setChromaLocInfoPresentFlag(m_chromaLocInfoPresentFlag);
		m_cTEncTop.setChromaSampleLocTypeTopField(m_chromaSampleLocTypeTopField);
		m_cTEncTop.setChromaSampleLocTypeBottomField(m_chromaSampleLocTypeBottomField);
		m_cTEncTop.setNeutralChromaIndicationFlag(m_neutralChromaIndicationFlag);
		m_cTEncTop.setDefaultDisplayWindow(m_defDispWinLeftOffset, m_defDispWinRightOffset, m_defDispWinTopOffset, m_defDispWinBottomOffset);
		m_cTEncTop.setFrameFieldInfoPresentFlag(m_frameFieldInfoPresentFlag);
		m_cTEncTop.setPocProportionalToTimingFlag(m_pocProportionalToTimingFlag);
		m_cTEncTop.setNumTicksPocDiffOneMinus1(m_numTicksPocDiffOneMinus1);
		m_cTEncTop.setBitstreamRestrictionFlag(m_bitstreamRestrictionFlag);
		m_cTEncTop.setTilesFixedStructureFlag(m_tilesFixedStructureFlag);
		m_cTEncTop.setMotionVectorsOverPicBoundariesFlag(m_motionVectorsOverPicBoundariesFlag);
		m_cTEncTop.setMinSpatialSegmentationIdc(m_minSpatialSegmentationIdc);
		m_cTEncTop.setMaxBytesPerPicDenom(m_maxBytesPerPicDenom);
		m_cTEncTop.setMaxBitsPerMinCuDenom(m_maxBitsPerMinCuDenom);
		m_cTEncTop.setLog2MaxMvLengthHorizontal(m_log2MaxMvLengthHorizontal);
		m_cTEncTop.setLog2MaxMvLengthVertical(m_log2MaxMvLengthVertical);
		m_cTEncTop.setEfficientFieldIRAPEnabled(m_bEfficientFieldIRAPEnabled);
		m_cTEncTop.setHarmonizeGopFirstFieldCoupleEnabled(m_bHarmonizeGopFirstFieldCoupleEnabled);

		m_cTEncTop.setSummaryOutFilename(m_summaryOutFilename);
		m_cTEncTop.setSummaryPicFilenameBase(m_summaryPicFilenameBase);
		m_cTEncTop.setSummaryVerboseness(m_summaryVerboseness);

#if LAYERS_NOT_PRESENT_SEI
		m_cTEncTop.setLayersNotPresentSEIEnabled(m_layersNotPresentSEIEnabled);
#endif  


#if SVC_EXTENSION
		if (layer != 0 && m_useInterLayerWeightedPred)
		{
			// Enable weighted prediction for enhancement layer
			m_cTEncTop.setUseWP(true);
			m_cTEncTop.setWPBiPred(true);
		}

	}
#endif
}

Void TAppEncTop::xCreateLib()
{
	// Video I/O
#if SVC_EXTENSION
  // initialize global variables
	initROM();

	for (UInt layer = 0; layer < m_numLayers; layer++)
	{
		//2
		// Video I/O
		m_apcTVideoIOYuvInputFile[layer]->open(m_apcLayerCfg[layer]->getInputFileName(), false, m_apcLayerCfg[layer]->m_inputBitDepth, m_apcLayerCfg[layer]->m_MSBExtendedBitDepth, m_apcLayerCfg[layer]->m_internalBitDepth);  // read  mode
		m_apcTVideoIOYuvInputFile[layer]->skipFrames(m_FrameSkip, m_apcLayerCfg[layer]->m_iSourceWidth - m_apcLayerCfg[layer]->m_aiPad[0], m_apcLayerCfg[layer]->m_iSourceHeight - m_apcLayerCfg[layer]->m_aiPad[1], m_apcLayerCfg[layer]->m_InputChromaFormatIDC);

		if (!m_apcLayerCfg[layer]->getReconFileName().empty())
		{
			m_apcTVideoIOYuvReconFile[layer]->open(m_apcLayerCfg[layer]->getReconFileName(), true, m_apcLayerCfg[layer]->m_outputBitDepth, m_apcLayerCfg[layer]->m_MSBExtendedBitDepth, m_apcLayerCfg[layer]->m_internalBitDepth);  // write mode
		}

		// Neo Decoder
		m_apcTEncTop[layer]->create();
	}
#else //SVC_EXTENSION
  // Video I/O
	m_cTVideoIOYuvInputFile.open(m_inputFileName, false, m_inputBitDepth, m_MSBExtendedBitDepth, m_internalBitDepth);  // read  mode
	m_cTVideoIOYuvInputFile.skipFrames(m_FrameSkip, m_iSourceWidth - m_aiPad[0], m_iSourceHeight - m_aiPad[1], m_InputChromaFormatIDC);

	if (!m_reconFileName.empty())
	{
		m_cTVideoIOYuvReconFile.open(m_reconFileName, true, m_outputBitDepth, m_outputBitDepth, m_internalBitDepth);  // write mode
	}

	// Neo Decoder
	m_cTEncTop.create();
#endif //SVC_EXTENSION
}

Void TAppEncTop::xDestroyLib()
{
	// Video I/O
#if SVC_EXTENSION
  // destroy ROM
	destroyROM();

	for (UInt layer = 0; layer < m_numLayers; layer++)
	{
		if (m_apcTVideoIOYuvInputFile[layer])
		{
			m_apcTVideoIOYuvInputFile[layer]->close();
			delete m_apcTVideoIOYuvInputFile[layer];
			m_apcTVideoIOYuvInputFile[layer] = NULL;
		}

		if (m_apcTVideoIOYuvReconFile[layer])
		{
			m_apcTVideoIOYuvReconFile[layer]->close();
			delete m_apcTVideoIOYuvReconFile[layer];
			m_apcTVideoIOYuvReconFile[layer] = NULL;
		}

		if (m_apcTEncTop[layer])
		{
			m_apcTEncTop[layer]->destroy();
			delete m_apcTEncTop[layer];
			m_apcTEncTop[layer] = NULL;
		}
	}
#else //SVC_EXTENSION
	m_cTVideoIOYuvInputFile.close();
	m_cTVideoIOYuvReconFile.close();

	// Neo Decoder
	m_cTEncTop.destroy();
#endif //SVC_EXTENSION
}

Void TAppEncTop::xInitLib(Bool isFieldCoding)
{
#if SVC_EXTENSION
	TComVPS* vps = m_apcTEncTop[0]->getVPS();
	m_apcTEncTop[0]->getVPS()->setMaxLayers(m_numLayers);

	UInt i = 0, dimIdLen = 0;
#if VIEW_SCALABILITY
	Int curDimId = 0;

	if (m_scalabilityMask[VIEW_ORDER_INDEX])
	{
		UInt maxViewOrderIndex = 0, maxViewId = 0;
		UInt voiLen = 1, vidLen = 1;

		for (i = 0; i < vps->getMaxLayers(); i++)
		{
			if (m_apcLayerCfg[i]->getViewOrderIndex() > maxViewOrderIndex)
			{
				maxViewOrderIndex = m_apcLayerCfg[i]->getViewOrderIndex();
			}

			if (m_apcLayerCfg[i]->getViewId() > maxViewId)
			{
				maxViewId = m_apcLayerCfg[i]->getViewId();
			}

		}
		while ((1 << voiLen) < (maxViewOrderIndex + 1))
		{
			voiLen++;
		}

		while ((1 << vidLen) < (maxViewId + 1))
		{
			vidLen++;
		}

		vps->setDimensionIdLen(0, voiLen);
		vps->setViewIdLen(vidLen);

		for (i = 0; i < vps->getMaxLayers(); i++)
		{
			vps->setDimensionId(i, 0, m_apcLayerCfg[i]->getViewOrderIndex());
		}

		for (i = 0; i < m_iNumberOfViews; i++)
		{
			vps->setViewIdVal(i, m_ViewIdVal[i]);
		}

		curDimId++;
	}
#endif

	while ((1 << dimIdLen) < m_numLayers)
	{
		dimIdLen++;
	}
#if VIEW_SCALABILITY
	vps->setDimensionIdLen(curDimId, dimIdLen);
#else
	vps->setDimensionIdLen(0, dimIdLen);
#endif
	vps->setNuhLayerIdPresentFlag(false);
	vps->setLayerIdInNuh(0, 0);
	vps->setLayerIdxInVps(0, 0);
	for (i = 1; i < vps->getMaxLayers(); i++)
	{
		vps->setLayerIdInNuh(i, m_apcLayerCfg[i]->m_layerId);
		vps->setLayerIdxInVps(vps->getLayerIdInNuh(i), i);
#if VIEW_SCALABILITY
		vps->setDimensionId(i, curDimId, i);
#else
		vps->setDimensionId(i, 0, i);
#endif
		if (m_apcLayerCfg[i]->m_layerId != i)
		{
			vps->setNuhLayerIdPresentFlag(true);
		}
	}

	for (UInt layer = 0; layer < m_numLayers; layer++)
	{
		//3
#if LAYER_CTB
		memcpy(g_auiZscanToRaster, g_auiLayerZscanToRaster[layer], sizeof(g_auiZscanToRaster));
		memcpy(g_auiRasterToZscan, g_auiLayerRasterToZscan[layer], sizeof(g_auiRasterToZscan));
		memcpy(g_auiRasterToPelX, g_auiLayerRasterToPelX[layer], sizeof(g_auiRasterToPelX));
		memcpy(g_auiRasterToPelY, g_auiLayerRasterToPelY[layer], sizeof(g_auiRasterToPelY));
#endif
		m_apcTEncTop[layer]->init(isFieldCoding);
	}

	// Set max-layer ID
	vps->setMaxLayerId(m_apcLayerCfg[m_numLayers - 1]->m_layerId);
	vps->setVpsExtensionFlag(m_numLayers > 1 ? true : false);

	if (m_numLayerSets > 1)
	{
		vps->setNumLayerSets(m_numLayerSets);

		for (Int setId = 1; setId < vps->getNumLayerSets(); setId++)
		{
			for (Int layerId = 0; layerId <= vps->getMaxLayerId(); layerId++)
			{
				vps->setLayerIdIncludedFlag(false, setId, layerId);
			}
		}
		for (Int setId = 1; setId < vps->getNumLayerSets(); setId++)
		{
			for (i = 0; i < m_numLayerInIdList[setId]; i++)
			{
				//4
				vps->setLayerIdIncludedFlag(true, setId, m_layerSetLayerIdList[setId][i]);
			}
		}
	}
	else
	{
		// Default layer sets
		vps->setNumLayerSets(m_numLayers);
		for (Int setId = 1; setId < vps->getNumLayerSets(); setId++)
		{
			for (Int layerIdx = 0; layerIdx <= vps->getMaxLayers(); layerIdx++)
			{
				//4
				UInt layerId = vps->getLayerIdInNuh(layerIdx);

				if (layerId <= setId)
				{
					vps->setLayerIdIncludedFlag(true, setId, layerId);
				}
				else
				{
					vps->setLayerIdIncludedFlag(false, setId, layerId);
				}
			}
		}
	}

	vps->setVpsNumLayerSetsMinus1(vps->getNumLayerSets() - 1);
	vps->setNumAddLayerSets(m_numAddLayerSets);
	vps->setNumLayerSets(vps->getNumLayerSets() + vps->getNumAddLayerSets());

	if (m_numAddLayerSets > 0)
	{
		for (Int setId = 0; setId < m_numAddLayerSets; setId++)
		{
			for (Int j = 0; j < m_numHighestLayerIdx[setId]; j++)
			{
				vps->setHighestLayerIdxPlus1(setId, j + 1, m_highestLayerIdx[setId][j] + 1);
			}
		}
	}

#if AVC_BASE
	vps->setNonHEVCBaseLayerFlag(m_nonHEVCBaseLayerFlag);
	if (m_nonHEVCBaseLayerFlag)
	{
		vps->setBaseLayerInternalFlag(false);
	}
#endif

	for (Int idx = vps->getBaseLayerInternalFlag() ? 2 : 1; idx < vps->getNumProfileTierLevel(); idx++)
	{
		vps->setProfilePresentFlag(idx, true);
	}

	vps->setSplittingFlag(false);

	for (i = 0; i < MAX_VPS_NUM_SCALABILITY_TYPES; i++)
	{
		vps->setScalabilityMask(i, false);
	}
	if (m_numLayers > 1)
	{
		Int scalabilityTypes = 0;
		for (i = 0; i < MAX_VPS_NUM_SCALABILITY_TYPES; i++)
		{
			vps->setScalabilityMask(i, m_scalabilityMask[i]);
			scalabilityTypes += m_scalabilityMask[i];
		}
#if VIEW_SCALABILITY
		assert(scalabilityTypes <= 3);
#else
#if AUXILIARY_PICTURES
		assert(scalabilityTypes <= 2);
#else
		assert(scalabilityTypes == 1);
#endif
#endif
		vps->setNumScalabilityTypes(scalabilityTypes);
	}
	else
	{
		vps->setNumScalabilityTypes(0);
	}

#if AUXILIARY_PICTURES
	if (m_scalabilityMask[AUX_ID])
	{
		UInt maxAuxId = 0;
		UInt auxDimIdLen = 1;
		Int  auxId = vps->getNumScalabilityTypes() - 1;
		for (i = 1; i < vps->getMaxLayers(); i++)
		{
			if (m_apcLayerCfg[i]->m_auxId > maxAuxId)
			{
				maxAuxId = m_apcLayerCfg[i]->m_auxId;
			}
		}
		while ((1 << auxDimIdLen) < (maxAuxId + 1))
		{
			auxDimIdLen++;
		}
		vps->setDimensionIdLen(auxId, auxDimIdLen);
		for (i = 1; i < vps->getMaxLayers(); i++)
		{
			vps->setDimensionId(i, auxId, m_apcLayerCfg[i]->m_auxId);
		}
	}
#endif

	vps->setMaxTSLayersPresentFlag(true);

	for (i = 0; i < vps->getMaxLayers(); i++)
	{
		vps->setMaxTSLayersMinus1(i, vps->getMaxTLayers() - 1);
	}

	vps->setMaxTidRefPresentFlag(m_maxTidRefPresentFlag);
	if (vps->getMaxTidRefPresentFlag())
	{
		for (i = 0; i < vps->getMaxLayers() - 1; i++)
		{
			for (Int j = i + 1; j < vps->getMaxLayers(); j++)
			{
				vps->setMaxTidIlRefPicsPlus1(i, j, m_apcTEncTop[i]->getMaxTidIlRefPicsPlus1());
			}
		}
	}
	else
	{
		for (i = 0; i < vps->getMaxLayers() - 1; i++)
		{
			for (Int j = i + 1; j < vps->getMaxLayers(); j++)
			{
				vps->setMaxTidIlRefPicsPlus1(i, j, 7);
			}
		}
	}

	vps->setDefaultRefLayersActiveFlag(false);

	// Direct reference layers
	UInt maxDirectRefLayers = 0;
	Bool isDefaultDirectDependencyTypeSet = false;
	for (UInt layerCtr = 1; layerCtr < vps->getMaxLayers(); layerCtr++)
	{
		UInt layerId = vps->getLayerIdInNuh(layerCtr);
		Int numDirectRefLayers = 0;

		vps->setNumDirectRefLayers(layerId, m_apcTEncTop[layerCtr]->getNumDirectRefLayers());
		maxDirectRefLayers = max<UInt>(maxDirectRefLayers, vps->getNumDirectRefLayers(layerId));

		for (i = 0; i < vps->getNumDirectRefLayers(layerId); i++)
		{
			vps->setRefLayerId(layerId, i, m_apcTEncTop[layerCtr]->getRefLayerId(i));
		}
		// Set direct dependency flag
		// Initialize flag to 0
		for (Int refLayerCtr = 0; refLayerCtr < layerCtr; refLayerCtr++)
		{
			vps->setDirectDependencyFlag(layerCtr, refLayerCtr, false);
		}
		for (i = 0; i < vps->getNumDirectRefLayers(layerId); i++)
		{
			vps->setDirectDependencyFlag(layerCtr, vps->getLayerIdxInVps(m_apcTEncTop[layerCtr]->getRefLayerId(i)), true);
		}
		// prediction indications
		vps->setDirectDepTypeLen(2); // sample and motion types are encoded
		for (Int refLayerCtr = 0; refLayerCtr < layerCtr; refLayerCtr++)
		{
			if (vps->getDirectDependencyFlag(layerCtr, refLayerCtr))
			{
				assert(m_apcTEncTop[layerCtr]->getSamplePredEnabledFlag(numDirectRefLayers) || m_apcTEncTop[layerCtr]->getMotionPredEnabledFlag(numDirectRefLayers));
				vps->setDirectDependencyType(layerCtr, refLayerCtr, ((m_apcTEncTop[layerCtr]->getSamplePredEnabledFlag(numDirectRefLayers) ? 1 : 0) |
					(m_apcTEncTop[layerCtr]->getMotionPredEnabledFlag(numDirectRefLayers) ? 2 : 0)) - 1);

				if (!isDefaultDirectDependencyTypeSet)
				{
					vps->setDefaultDirectDependecyTypeFlag(1);
					vps->setDefaultDirectDependecyType(vps->getDirectDependencyType(layerCtr, refLayerCtr));
					isDefaultDirectDependencyTypeSet = true;
				}
				else if (vps->getDirectDependencyType(layerCtr, refLayerCtr) != vps->getDefaultDirectDependencyType())
				{
					vps->setDefaultDirectDependecyTypeFlag(0);
				}

				numDirectRefLayers++;
			}
			else
			{
				vps->setDirectDependencyType(layerCtr, refLayerCtr, 0);
			}
		}
	}

	// dependency constraint
	vps->setNumRefLayers();

	if (vps->getMaxLayers() > MAX_REF_LAYERS)
	{
		for (UInt layerCtr = 1; layerCtr <= vps->getMaxLayers() - 1; layerCtr++)
		{
			assert(vps->getNumRefLayers(vps->getLayerIdInNuh(layerCtr)) <= MAX_REF_LAYERS);
		}
	}

	// The Layer ID List variables should be derived here.
	vps->deriveLayerIdListVariables();
	vps->setPredictedLayerIds();
	vps->setTreePartitionLayerIdList();
	vps->deriveLayerIdListVariablesForAddLayerSets();

	vps->setDefaultTargetOutputLayerIdc(m_defaultTargetOutputLayerIdc); // As per configuration file

	if (m_numOutputLayerSets == -1)  // # of output layer sets not specified in the configuration file
	{
		vps->setNumOutputLayerSets(vps->getNumLayerSets());

		for (i = 1; i < vps->getNumLayerSets(); i++)
		{
			vps->setOutputLayerSetIdx(i, i);
		}
	}
	else
	{
		vps->setNumOutputLayerSets(m_numOutputLayerSets);
		for (Int olsCtr = 0; olsCtr < vps->getNumLayerSets(); olsCtr++) // Default output layer sets
		{
			vps->setOutputLayerSetIdx(olsCtr, olsCtr);
		}
		for (Int olsCtr = vps->getNumLayerSets(); olsCtr < vps->getNumOutputLayerSets(); olsCtr++)  // Non-default output layer sets
		{
			vps->setOutputLayerSetIdx(olsCtr, m_outputLayerSetIdx[olsCtr - vps->getNumLayerSets()]);
		}
	}

	// Target output layer
	vps->deriveNumberOfSubDpbs();
	vps->setOutputLayerFlag(0, 0, 1);

	// derive OutputLayerFlag[i][j] 
	// default_output_layer_idc equal to 1 specifies that only the layer with the highest value of nuh_layer_id such that nuh_layer_id equal to nuhLayerIdA and 
	// AuxId[ nuhLayerIdA ] equal to 0 in each of the output layer sets with index in the range of 1 to vps_num_layer_sets_minus1, inclusive, is an output layer of its output layer set.

	// Include the highest layer as output layer for each layer set
	for (Int lsIdx = 1; lsIdx <= vps->getVpsNumLayerSetsMinus1(); lsIdx++)
	{
		for (UInt layer = 0; layer < vps->getNumLayersInIdList(lsIdx); layer++)
		{
			switch (vps->getDefaultTargetOutputLayerIdc())
			{
			case 0: vps->setOutputLayerFlag(lsIdx, layer, 1);
				break;
			case 1: vps->setOutputLayerFlag(lsIdx, layer, layer == vps->getNumLayersInIdList(lsIdx) - 1);
				break;
			case 2:
			case 3: vps->setOutputLayerFlag(lsIdx, layer, (layer != vps->getNumLayersInIdList(lsIdx) - 1) ? std::find(m_listOfOutputLayers[lsIdx].begin(), m_listOfOutputLayers[lsIdx].end(), m_layerSetLayerIdList[lsIdx][layer]) != m_listOfOutputLayers[lsIdx].end()
				: m_listOfOutputLayers[lsIdx][m_listOfOutputLayers[lsIdx].size() - 1] == m_layerSetLayerIdList[lsIdx][layer]);
				break;
			}
		}
	}

	for (Int olsIdx = vps->getVpsNumLayerSetsMinus1() + 1; olsIdx < vps->getNumOutputLayerSets(); olsIdx++)
	{
		for (UInt layer = 0; layer < vps->getNumLayersInIdList(vps->getOutputLayerSetIdx(olsIdx)); layer++)
		{
			vps->setOutputLayerFlag(olsIdx, layer, (layer != vps->getNumLayersInIdList(vps->getOutputLayerSetIdx(olsIdx)) - 1) ? std::find(m_listOfOutputLayers[olsIdx].begin(), m_listOfOutputLayers[olsIdx].end(), vps->getLayerSetLayerIdList(vps->getOutputLayerSetIdx(olsIdx), layer)) != m_listOfOutputLayers[olsIdx].end()
				: m_listOfOutputLayers[olsIdx][m_listOfOutputLayers[olsIdx].size() - 1] == vps->getLayerSetLayerIdList(vps->getOutputLayerSetIdx(olsIdx), layer));
		}
	}

	vps->deriveNecessaryLayerFlag();
	vps->checkNecessaryLayerFlagCondition();
	vps->calculateMaxSLInLayerSets();

	// Initialize dpb_size_table() for all ouput layer sets in the VPS extension
	for (i = 1; i < vps->getNumOutputLayerSets(); i++)
	{
		Int layerSetIdxForOutputLayerSet = vps->getOutputLayerSetIdx(i);
		Int layerSetId = vps->getOutputLayerSetIdx(i);

		for (Int j = 0; j < vps->getMaxTLayers(); j++)
		{

			Int maxNumReorderPics = -1;
			for (Int k = 0; k < vps->getNumSubDpbs(layerSetIdxForOutputLayerSet); k++)
			{
				Int layerId = vps->getLayerSetLayerIdList(layerSetId, k); // k-th layer in the output layer set
				vps->setMaxVpsDecPicBufferingMinus1(i, k, j, m_apcTEncTop[vps->getLayerIdxInVps(layerId)]->getMaxDecPicBuffering(j) - 1);
				maxNumReorderPics = std::max(maxNumReorderPics, m_apcTEncTop[vps->getLayerIdxInVps(layerId)]->getNumReorderPics(j));
			}
			vps->setMaxVpsNumReorderPics(i, j, maxNumReorderPics);
			vps->determineSubDpbInfoFlags();
		}
	}

	vps->setMaxOneActiveRefLayerFlag(maxDirectRefLayers > 1 ? false : true);

	// POC LSB not present flag
	for (i = 1; i < vps->getMaxLayers(); i++)
	{
		if (vps->getNumDirectRefLayers(vps->getLayerIdInNuh(i)) == 0)
		{
			// make independedent layers base-layer compliant
			vps->setPocLsbNotPresentFlag(i, true);
		}
	}

	vps->setCrossLayerPictureTypeAlignFlag(m_crossLayerPictureTypeAlignFlag);
	vps->setCrossLayerAlignedIdrOnlyFlag(m_crossLayerAlignedIdrOnlyFlag);
	vps->setCrossLayerIrapAlignFlag(m_crossLayerIrapAlignFlag);

	if (vps->getCrossLayerPictureTypeAlignFlag())
	{
		// When not present, the value of cross_layer_irap_aligned_flag is inferred to be equal to vps_vui_present_flag,   
		assert(m_crossLayerIrapAlignFlag == true);
		vps->setCrossLayerIrapAlignFlag(true);
	}

	for (UInt layerCtr = 1; layerCtr < vps->getMaxLayers(); layerCtr++)
	{
		for (Int refLayerCtr = 0; refLayerCtr < layerCtr; refLayerCtr++)
		{
			if (vps->getDirectDependencyFlag(layerCtr, refLayerCtr))
			{
				assert(layerCtr < MAX_LAYERS);

				if (m_apcTEncTop[layerCtr]->getIntraPeriod() != m_apcTEncTop[refLayerCtr]->getIntraPeriod())
				{
					vps->setCrossLayerIrapAlignFlag(false);
					break;
				}
			}
		}
	}
	vps->setSingleLayerForNonIrapFlag(m_adaptiveResolutionChange > 0 ? true : false);
	vps->setHigherLayerIrapSkipFlag(m_skipPictureAtArcSwitch);

	for (Int k = 0; k < MAX_VPS_LAYER_SETS_PLUS1; k++)
	{
		vps->setAltOuputLayerFlag(k, m_altOutputLayerFlag);
	}

	// VPS VUI BSP HRD parameters
	vps->setVpsVuiBspHrdPresentFlag(false);
	TEncTop *pcCfg = m_apcTEncTop[0];
	if (pcCfg->getBufferingPeriodSEIEnabled())
	{
		Int j;
		vps->setVpsVuiBspHrdPresentFlag(true);
		vps->setVpsNumAddHrdParams(vps->getMaxLayers());
		vps->createBspHrdParamBuffer(vps->getVpsNumAddHrdParams() + 1);
		for (i = vps->getNumHrdParameters(), j = 0; i < vps->getNumHrdParameters() + vps->getVpsNumAddHrdParams(); i++, j++)
		{
			vps->setCprmsAddPresentFlag(j, true);
			vps->setNumSubLayerHrdMinus1(j, vps->getMaxTLayers() - 1);

			UInt layerIdx = j;
			TEncTop *pcCfgLayer = m_apcTEncTop[layerIdx];

			Int iPicWidth = pcCfgLayer->getSourceWidth();
			Int iPicHeight = pcCfgLayer->getSourceHeight();

			UInt uiWidthInCU = (iPicWidth  % m_apcLayerCfg[layerIdx]->m_uiMaxCUWidth) ? iPicWidth / m_apcLayerCfg[layerIdx]->m_uiMaxCUWidth + 1 : iPicWidth / m_apcLayerCfg[layerIdx]->m_uiMaxCUWidth;
			UInt uiHeightInCU = (iPicHeight % m_apcLayerCfg[layerIdx]->m_uiMaxCUHeight) ? iPicHeight / m_apcLayerCfg[layerIdx]->m_uiMaxCUHeight + 1 : iPicHeight / m_apcLayerCfg[layerIdx]->m_uiMaxCUHeight;
			UInt maxCU = pcCfgLayer->getSliceArgument() >> (m_apcLayerCfg[layerIdx]->m_uiMaxCUDepth << 1);

			UInt uiNumCUsInFrame = uiWidthInCU * uiHeightInCU;

			UInt numDU = (pcCfgLayer->getSliceMode() == 1) ? (uiNumCUsInFrame / maxCU) : (0);
			if (uiNumCUsInFrame % maxCU != 0 || numDU == 0)
			{
				numDU++;
			}
			//vps->getBspHrd(i)->setNumDU( numDU );
			vps->setBspHrdParameters(i, pcCfgLayer->getFrameRate(), numDU, pcCfgLayer->getTargetBitrate(), (pcCfgLayer->getIntraPeriod() > 0));
		}

		// Signalling of additional partitioning schemes
		for (Int h = 1; h < vps->getNumOutputLayerSets(); h++)
		{
			Int lsIdx = vps->getOutputLayerSetIdx(h);
			vps->setNumSignalledPartitioningSchemes(h, 1);  // Only the default per-layer partitioning scheme
			for (j = 1; j < vps->getNumSignalledPartitioningSchemes(h); j++)
			{
				// ToDo: Add code for additional partitioning schemes here
				// ToDo: Initialize num_partitions_in_scheme_minus1 and layer_included_in_partition_flag
			}

			for (i = 0; i < vps->getNumSignalledPartitioningSchemes(h); i++)
			{
				if (i == 0)
				{
					for (Int t = 0; t <= vps->getMaxSLayersInLayerSetMinus1(lsIdx); t++)
					{
						vps->setNumBspSchedulesMinus1(h, i, t, 0);
						for (j = 0; j <= vps->getNumBspSchedulesMinus1(h, i, t); j++)
						{
							for (Int k = 0; k <= vps->getNumPartitionsInSchemeMinus1(h, i); k++)
							{
								// Only for the default partition
								Int nuhlayerId = vps->getLayerSetLayerIdList(lsIdx, k);
								Int layerIdxInVps = vps->getLayerIdxInVps(nuhlayerId);
								vps->setBspHrdIdx(h, i, t, j, k, layerIdxInVps + vps->getNumHrdParameters());

								vps->setBspSchedIdx(h, i, t, j, k, 0);
							}
						}
					}
				}
				else
				{
					assert(0);    // Need to add support for additional partitioning schemes.
				}
			}
		}
	}
#else //SVC_EXTENSION
	m_cTEncTop.init(isFieldCoding);
#endif //SVC_EXTENSION
}

// ====================================================================================================================
// Public member functions
// ====================================================================================================================

/**
 - create internal class
 - initialize internal variable
 - until the end of input YUV file, call encoding function in TEncTop class
 - delete allocated buffers
 - destroy internal class
 .
 */
#if SVC_EXTENSION
Void TAppEncTop::encode()
{
	fstream bitstreamFile(m_bitstreamFileName.c_str(), fstream::binary | fstream::out);
	if (!bitstreamFile)
	{
		fprintf(stderr, "\nfailed to open bitstream file `%s' for writing\n", m_bitstreamFileName.c_str());
		exit(EXIT_FAILURE);
	}

	// allocated memory
	for (Int layer = 0; layer < m_numLayers; layer++)
	{
		m_apcTVideoIOYuvInputFile[layer] = new TVideoIOYuv;
		m_apcTVideoIOYuvReconFile[layer] = new TVideoIOYuv;
		m_apcTEncTop[layer] = new TEncTop;
	}

	TComPicYuv*       pcPicYuvOrg[MAX_LAYERS];
	TComPicYuv*       pcPicYuvRec = NULL;

	// initialize internal class & member variables
	xInitLibCfg();
	xCreateLib();
	xInitLib(m_isField);

	// main encoder loop
	Int   iNumEncoded = 0, iTotalNumEncoded = 0;
	Bool  bEos = false;

	const InputColourSpaceConversion ipCSC = m_inputColourSpaceConvert;
	const InputColourSpaceConversion snrCSC = (!m_snrInternalColourSpace) ? m_inputColourSpaceConvert : IPCOLOURSPACE_UNCHANGED;

	list<AccessUnit> outputAccessUnits; ///< list of access units to write out.  is populated by the encoding process

	TComPicYuv acPicYuvTrueOrg[MAX_LAYERS];

	for (UInt layer = 0; layer < m_numLayers; layer++)
	{
		//5
		// allocate original YUV buffer
		pcPicYuvOrg[layer] = new TComPicYuv;
		if (m_isField)
		{
			pcPicYuvOrg[layer]->create(m_apcLayerCfg[layer]->m_iSourceWidth, m_apcLayerCfg[layer]->m_iSourceHeightOrg, m_apcLayerCfg[layer]->m_chromaFormatIDC, m_apcLayerCfg[layer]->m_uiMaxCUWidth, m_apcLayerCfg[layer]->m_uiMaxCUHeight, m_apcLayerCfg[layer]->m_uiMaxTotalCUDepth, true, NULL);
			acPicYuvTrueOrg[layer].create(m_apcLayerCfg[layer]->m_iSourceWidth, m_apcLayerCfg[layer]->m_iSourceHeightOrg, m_apcLayerCfg[layer]->m_chromaFormatIDC, m_apcLayerCfg[layer]->m_uiMaxCUWidth, m_apcLayerCfg[layer]->m_uiMaxCUHeight, m_apcLayerCfg[layer]->m_uiMaxTotalCUDepth, true, NULL);
		}
		else
		{
			pcPicYuvOrg[layer]->create(m_apcLayerCfg[layer]->m_iSourceWidth, m_apcLayerCfg[layer]->m_iSourceHeight, m_apcLayerCfg[layer]->m_chromaFormatIDC, m_apcLayerCfg[layer]->m_uiMaxCUWidth, m_apcLayerCfg[layer]->m_uiMaxCUHeight, m_apcLayerCfg[layer]->m_uiMaxTotalCUDepth, true, NULL);
			acPicYuvTrueOrg[layer].create(m_apcLayerCfg[layer]->m_iSourceWidth, m_apcLayerCfg[layer]->m_iSourceHeight, m_apcLayerCfg[layer]->m_chromaFormatIDC, m_apcLayerCfg[layer]->m_uiMaxCUWidth, m_apcLayerCfg[layer]->m_uiMaxCUHeight, m_apcLayerCfg[layer]->m_uiMaxTotalCUDepth, true, NULL);
		}
	}

#if Machine_Learning_Debug //LXY
	// 每一层的SVM模型参数
	Int *WIDTH = new Int[m_numLayers];
	Int *HEIGHT = new Int[m_numLayers];

	// EL CU的特征数增加了同位BL CU的划分深度、PU模式
#if FULL_FEATURE
	Int Feature_Num_Level0[2] = { 13, 13 };
	Int Feature_Num_Level1[2] = { 13, 13 };
	Int Feature_Num_Level2[2] = { 13, 13 };//the number of features
#endif
#if SELECTED_FEATURE
	Int Feature_Num_Level0[2] = { 7, 8 };
	Int Feature_Num_Level1[2] = { 4, 8 };
	Int Feature_Num_Level2[2] = { 12, 8 };//the number of features
#endif

	Int *frameSize0 = new Int[m_numLayers];
	Int *frameSize1 = new Int[m_numLayers];
	Int *frameSize2 = new Int[m_numLayers];
	Int *W0 = new Int[m_numLayers];
	Int *W1 = new Int[m_numLayers];
	Int *W2 = new Int[m_numLayers];
	Int *H0 = new Int[m_numLayers];
	Int *H1 = new Int[m_numLayers];
	Int *H2 = new Int[m_numLayers];

	//------------------------------------------------------------------//
	Int trainframe = trainGOP * m_iGOPSize;
	//------------------------------------------------------------------//

	Double **Feature0, **Feature1, **Feature2;
	Feature0 = new Double *[m_numLayers];
	Feature1 = new Double *[m_numLayers];
	Feature2 = new Double *[m_numLayers];

	Double **Feature00, **Feature11, **Feature22;
	Feature00 = new Double *[m_numLayers];
	Feature11 = new Double *[m_numLayers];
	Feature22 = new Double *[m_numLayers];

	Int **Truth0, **Truth1, **Truth2;
	Truth0 = new Int *[m_numLayers];
	Truth1 = new Int *[m_numLayers];
	Truth2 = new Int *[m_numLayers];

#if PUMode_FastDecision
	Int **Truth0_PU, **Truth1_PU, **Truth2_PU;
	Truth0_PU = new Int *[m_numLayers];
	Truth1_PU = new Int *[m_numLayers];
	Truth2_PU = new Int *[m_numLayers];
#endif

	maxmin **M0, **M1, **M2;
	M0 = new maxmin *[m_numLayers];
	M1 = new maxmin *[m_numLayers];
	M2 = new maxmin *[m_numLayers];

	svm_model **model0, **model1, **model2;
	model0 = new svm_model *[m_numLayers];
	model1 = new svm_model *[m_numLayers];
	model2 = new svm_model *[m_numLayers];

#if PUMode_FastDecision
	svm_model **model0_PU, **model1_PU, **model2_PU;
	model0_PU = new svm_model *[m_numLayers];
	model1_PU = new svm_model *[m_numLayers];
	model2_PU = new svm_model *[m_numLayers];
#endif

	Bool *training = new Bool[m_numLayers];  // SVM模型训练开关

#if Offline
	FILE **paramater0, **paramater1, **paramater2;
	paramater0 = new FILE *[m_numLayers];
	paramater1 = new FILE *[m_numLayers];
	paramater2 = new FILE *[m_numLayers];
#endif

	for (UInt layer = 0; layer < m_numLayers; layer++)
	{
		WIDTH[layer] = m_apcLayerCfg[layer]->m_iSourceWidth;
		HEIGHT[layer] = m_apcLayerCfg[layer]->m_iSourceHeight;

		//Int trainGOP = 1;
			//--------------------------边界考虑---------------------------------//
		if (WIDTH[layer] % 64 == 0 && HEIGHT[layer] % 64 == 0)
			frameSize0[layer] = WIDTH[layer] / 64 * HEIGHT[layer] / 64;
		else if (WIDTH[layer] % 64 != 0 && HEIGHT[layer] % 64 == 0)
			frameSize0[layer] = (WIDTH[layer] / 64 + 1)*HEIGHT[layer] / 64;
		else if (WIDTH[layer] % 64 == 0 && HEIGHT[layer] % 64 != 0)
			frameSize0[layer] = WIDTH[layer] / 64 * (HEIGHT[layer] / 64 + 1);
		else if (WIDTH[layer] % 64 != 0 && HEIGHT[layer] % 64 != 0)
			frameSize0[layer] = (WIDTH[layer] / 64 + 1)*(HEIGHT[layer] / 64 + 1);
		//------------------------------------------------------------------//
		frameSize1[layer] = 4 * frameSize0[layer]; frameSize2[layer] = 16 * frameSize0[layer];

		if (WIDTH[layer] % 64 == 0)  W0[layer] = WIDTH[layer] / 64;
		else              W0[layer] = WIDTH[layer] / 64 + 1;

		W1[layer] = 2 * W0[layer]; W2[layer] = 4 * W0[layer];

		if (HEIGHT[layer] % 64 == 0) H0[layer] = HEIGHT[layer] / 64;
		else              H0[layer] = HEIGHT[layer] / 64 + 1;

		H1[layer] = 2 * H0[layer]; H2[layer] = 4 * H0[layer];

#if Saliency_Weight
		Mat img(Size(WIDTH[layer], HEIGHT[layer]), CV_8UC3);
		Mat img_output(Size(WIDTH[layer], HEIGHT[layer]), CV_8UC3);
#endif


		Feature0[layer] = new Double[trainframe*frameSize0[layer] * Feature_Num_Level0[layer]];
		Feature1[layer] = new Double[trainframe*frameSize1[layer] * Feature_Num_Level1[layer]];
		Feature2[layer] = new Double[trainframe*frameSize2[layer] * Feature_Num_Level2[layer]];
		memset(Feature0[layer], 0, sizeof(Double)*trainframe*frameSize0[layer] * Feature_Num_Level0[layer]);
		memset(Feature1[layer], 0, sizeof(Double)*trainframe*frameSize1[layer] * Feature_Num_Level1[layer]);
		memset(Feature2[layer], 0, sizeof(Double)*trainframe*frameSize2[layer] * Feature_Num_Level2[layer]);


		Feature00[layer] = new Double[trainframe*frameSize0[layer] * Feature_Num_Level0[layer]];
		Feature11[layer] = new Double[trainframe*frameSize1[layer] * Feature_Num_Level1[layer]];
		Feature22[layer] = new Double[trainframe*frameSize2[layer] * Feature_Num_Level2[layer]];
		memset(Feature00[layer], 0, sizeof(Double)*trainframe*frameSize0[layer] * Feature_Num_Level0[layer]);
		memset(Feature11[layer], 0, sizeof(Double)*trainframe*frameSize1[layer] * Feature_Num_Level1[layer]);
		memset(Feature22[layer], 0, sizeof(Double)*trainframe*frameSize2[layer] * Feature_Num_Level2[layer]);


		Truth0[layer] = new Int[trainframe*frameSize0[layer]];
		Truth1[layer] = new Int[trainframe*frameSize1[layer]];
		Truth2[layer] = new Int[trainframe*frameSize2[layer]];
		memset(Truth0[layer], 0, sizeof(Int)*trainframe*frameSize0[layer]);
		memset(Truth1[layer], 0, sizeof(Int)*trainframe*frameSize1[layer]);
		memset(Truth2[layer], 0, sizeof(Int)*trainframe*frameSize2[layer]);

#if PUMode_FastDecision
		Truth0_PU[layer] = new Int[trainframe*frameSize0[layer]];
		Truth1_PU[layer] = new Int[trainframe*frameSize1[layer]];
		Truth2_PU[layer] = new Int[trainframe*frameSize2[layer]];
		memset(Truth0_PU[layer], 0, sizeof(Int)*trainframe*frameSize0[layer]);
		memset(Truth1_PU[layer], 0, sizeof(Int)*trainframe*frameSize1[layer]);
		memset(Truth2_PU[layer], 0, sizeof(Int)*trainframe*frameSize2[layer]);
#endif

		M0[layer] = new maxmin[Feature_Num_Level0[layer]];
		M1[layer] = new maxmin[Feature_Num_Level1[layer]];
		M2[layer] = new maxmin[Feature_Num_Level2[layer]];

#if Online
		model0[layer] = NULL, model1[layer] = NULL, model2[layer] = NULL;
#if PUMode_FastDecision
		model0_PU[layer] = NULL, model1_PU[layer] = NULL, model2_PU[layer] = NULL;
#endif
#endif
		training[layer] = false;
		//------------------------------------------------------------------//
#if Offline
		switch (layer)
		{
		case 0: // layer 0
			model0[layer] = svm_load_model("model0_l0.txt");
			model1[layer] = svm_load_model("model1_l0.txt");
			model2[layer] = svm_load_model("model2_l0.txt");
			model3[layer] = svm_load_model("model3_l0.txt");

			paramater0[layer] = fopen("par0_l0.txt", "rb");
			paramater1[layer] = fopen("par1_l0.txt", "rb");
			paramater2[layer] = fopen("par2_l0.txt", "rb");
			break;
		case 1: // layer 1
			model0[layer] = svm_load_model("model0_l1.txt");
			model1[layer] = svm_load_model("model1_ll.txt");
			model2[layer] = svm_load_model("model2_l1.txt");
			model3[layer] = svm_load_model("model3_l1.txt");

			paramater0[layer] = fopen("par0_l1.txt", "rb");
			paramater1[layer] = fopen("par1_l1.txt", "rb");
			paramater2[layer] = fopen("par2_l1.txt", "rb");
		default:
			break;
		}

		TChar string_temp; Int classifier[2]; Int index0;
		//==============================================================================
		fseek(paramater0[layer], 0, 0);
		fscanf(paramater0[layer], "%c", &string_temp);
		fscanf(paramater0[layer], "%d %d", &classifier[0], &classifier[1]);
		for (Int k = 0; k < Feature_Num_Level0[layer]; k++)
		{
			fscanf(paramater0[layer], "%d %lf %lf", &index0, &M0[k].minvalue, &M0[k].maxvalue);
		}
		//==============================================================================
		fseek(paramater1[layer], 0, 0);
		fscanf(paramater1[layer], "%c", &string_temp);
		fscanf(paramater1[layer], "%d %d", &classifier[0], &classifier[1]);
		for (Int k = 0; k < Feature_Num_Level1[layer]; k++)
		{
			fscanf(paramater1[layer], "%d %lf %lf", &index0, &M1[k].minvalue, &M1[k].maxvalue);
		}
		//==============================================================================
		fseek(paramater2[layer], 0, 0);
		fscanf(paramater2[layer], "%c", &string_temp);
		fscanf(paramater2[layer], "%d %d", &classifier[0], &classifier[1]);
		for (Int k = 0; k < Feature_Num_Level2[layer]; k++)
		{
			fscanf(paramater2[layer], "%d %lf %lf", &index0, &M2[k].minvalue, &M2[k].maxvalue);
		}
#endif
	}

#endif

	Bool bFirstFrame = true;
	while (!bEos)
	{
		// Read enough frames
		Bool bFramesReadyToCode = false;
		while (!bFramesReadyToCode)
		{
			for (UInt layer = 0; layer < m_numLayers; layer++)
			{
				//6
				// get buffers
				xGetBuffer(pcPicYuvRec, layer);

				// read input YUV file
				m_apcTVideoIOYuvInputFile[layer]->read(pcPicYuvOrg[layer], &acPicYuvTrueOrg[layer], ipCSC, m_apcLayerCfg[layer]->m_aiPad, m_apcLayerCfg[layer]->m_InputChromaFormatIDC, m_bClipInputVideoToRec709Range);

#if AUXILIARY_PICTURES
				if (m_apcLayerCfg[layer]->m_chromaFormatIDC == CHROMA_400 || (m_apcTEncTop[0]->getVPS()->getScalabilityMask(AUX_ID) && (m_apcLayerCfg[layer]->m_auxId == AUX_ALPHA || m_apcLayerCfg[layer]->m_auxId == AUX_DEPTH)))
				{
					pcPicYuvOrg[layer]->convertToMonochrome(m_apcLayerCfg[layer]->m_internalBitDepth[CHANNEL_TYPE_CHROMA]);
				}
#endif

				if (layer == m_numLayers - 1)
				{
					// increase number of received frames  每次接收一个GOP
					m_iFrameRcvd++;
					// check end of file
					bEos = (m_isField && (m_iFrameRcvd == (m_framesToBeEncoded >> 1))) || (!m_isField && (m_iFrameRcvd == m_framesToBeEncoded));
				}

				if (m_isField)
				{
					m_apcTEncTop[layer]->encodePrep(pcPicYuvOrg[layer], &acPicYuvTrueOrg[layer], m_isTopFieldFirst);
				}
				else
				{
					m_apcTEncTop[layer]->encodePrep(pcPicYuvOrg[layer], &acPicYuvTrueOrg[layer]);
				}
			}

			// 准备编码的图像为一个GOP
			bFramesReadyToCode = !(!bFirstFrame && (m_apcTEncTop[m_numLayers - 1]->getNumPicRcvd() != m_iGOPSize && m_iGOPSize) && !bEos);
		}
		Bool flush = 0;
		// if end of file (which is only detected on a read failure) flush the encoder of any queued pictures
		if (m_apcTVideoIOYuvInputFile[m_numLayers - 1]->isEof())
		{
			flush = true;
			bEos = true;
			m_iFrameRcvd--;
			m_apcTEncTop[m_numLayers - 1]->setFramesToBeEncoded(m_iFrameRcvd);
		}

#if RC_SHVC_HARMONIZATION
		for (UInt layer = 0; layer < m_numLayers; layer++)
		{
			if (m_apcTEncTop[layer]->getUseRateCtrl())
			{
				m_apcTEncTop[layer]->getRateCtrl()->initRCGOP(m_apcTEncTop[layer]->getNumPicRcvd());
			}
		}
#endif

		if (m_adaptiveResolutionChange)
		{
			for (UInt layer = 0; layer < m_numLayers; layer++)
			{
				TComList<TComPicYuv*>::iterator iterPicYuvRec;
				for (iterPicYuvRec = m_acListPicYuvRec[layer].begin(); iterPicYuvRec != m_acListPicYuvRec[layer].end(); iterPicYuvRec++)
				{
					TComPicYuv* recPic = *(iterPicYuvRec);
					recPic->setReconstructed(false);
				}
			}
		}

		// loop through frames in one GOP
		for (UInt iPicIdInGOP = 0; iPicIdInGOP < (bFirstFrame ? 1 : m_iGOPSize); iPicIdInGOP++)
		{
			// layer by layer for each frame
			for (UInt layer = 0; layer < m_numLayers; layer++)
			{
				//7
#if LAYER_CTB
				memcpy(g_auiZscanToRaster, g_auiLayerZscanToRaster[layer], sizeof(g_auiZscanToRaster));
				memcpy(g_auiRasterToZscan, g_auiLayerRasterToZscan[layer], sizeof(g_auiRasterToZscan));
				memcpy(g_auiRasterToPelX, g_auiLayerRasterToPelX[layer], sizeof(g_auiRasterToPelX));
				memcpy(g_auiRasterToPelY, g_auiLayerRasterToPelY[layer], sizeof(g_auiRasterToPelY));
#endif

#if Machine_Learning_Debug
#if Online
				if (training[layer] == true)
				{
					cout << " classifier generating........................................." << endl;
#if Online
					SVM_Train_Online(Feature0[layer], Feature1[layer], Feature2[layer], Truth0[layer], Truth1[layer], Truth2[layer],
#if PUMode_FastDecision
						Truth0_PU[layer], Truth1_PU[layer], Truth2_PU[layer],
#endif
						M0[layer], M1[layer], M2[layer],
						Feature_Num_Level0[layer], Feature_Num_Level1[layer], Feature_Num_Level2[layer], trainframe, frameSize0[layer], frameSize1[layer], frameSize2[layer], W0[layer], W1[layer], W2[layer],
						WIDTH[layer], HEIGHT[layer], model0[layer], model1[layer], model2[layer]
#if PUMode_FastDecision
						, model0_PU[layer], model1_PU[layer], model2_PU[layer]
#endif
					);
#endif
					training[layer] = false;

					cout << " classifier finished............................................" << endl;
				}
#endif

				if (bFirstFrame || layer == 0)//Intra、基本层直接编码
				{
					if (m_isField)
					{
						m_apcTEncTop[layer]->encode(flush ? 0 : pcPicYuvOrg[layer], snrCSC, m_acListPicYuvRec[layer], outputAccessUnits, iPicIdInGOP, m_isTopFieldFirst);
					}
					else
					{
						m_apcTEncTop[layer]->encode(flush ? 0 : pcPicYuvOrg[layer], snrCSC, m_acListPicYuvRec[layer], outputAccessUnits, iPicIdInGOP);
					}
				}
#if Online
				else if (((m_iFrameRcvd - 5) / 4) % 25 == 0)//train   适用于LDP，对于RA，需要对应调整，周期性更新模型，每隔100frames
				// else if ((((m_iFrameRcvd - 5) / 4) % 25 == 0) || (((m_iFrameRcvd - 5) / 4) % 25 == 1))
				{
					if (m_isField)
					{
						m_apcTEncTop[layer]->encode_train(Feature0[layer], Feature1[layer], Feature2[layer],
							Truth0[layer], Truth1[layer], Truth2[layer],
#if PUMode_FastDecision
							Truth0_PU[layer], Truth1_PU[layer], Truth2_PU[layer],
#endif
							frameSize0[layer], frameSize1[layer], frameSize2[layer], Feature_Num_Level0[layer], Feature_Num_Level1[layer], Feature_Num_Level2[layer],
							flush ? 0 : pcPicYuvOrg[layer], snrCSC, m_acListPicYuvRec[layer], outputAccessUnits, iPicIdInGOP, m_isTopFieldFirst);
					}
					else
					{
						m_apcTEncTop[layer]->encode_train(Feature0[layer], Feature1[layer], Feature2[layer],
							Truth0[layer], Truth1[layer], Truth2[layer],
#if PUMode_FastDecision
							Truth0_PU[layer], Truth1_PU[layer], Truth2_PU[layer],
#endif
							frameSize0[layer], frameSize1[layer], frameSize2[layer], Feature_Num_Level0[layer], Feature_Num_Level1[layer], Feature_Num_Level2[layer],
							flush ? 0 : pcPicYuvOrg[layer], snrCSC, m_acListPicYuvRec[layer], outputAccessUnits, iPicIdInGOP);
					}
					if (iPicIdInGOP == m_iGOPSize - 1) //适用于LDP，RA需要对应调整，GOP的最后一帧
					// if ((iPicIdInGOP == m_iGOPSize - 1) && (((m_iFrameRcvd - 5) / 4) % 25 == 1))
					{
						training[layer] = true;
					}
				}
#endif
				else //predict
				{
#if Online || Offline
					if (m_isField)
					{
						m_apcTEncTop[layer]->encode_predict_online(Feature00[layer], Feature11[layer], Feature22[layer],
							model0[layer], model1[layer], model2[layer],
#if PUMode_FastDecision
							model0_PU[layer], model1_PU[layer], model2_PU[layer],
#endif
							M0[layer], M1[layer], M2[layer],
							frameSize0[layer], frameSize1[layer], frameSize2[layer], Feature_Num_Level0[layer], Feature_Num_Level1[layer], Feature_Num_Level2[layer],
#if Saliency_Weight
							img, img_output,
#endif
							flush ? 0 : pcPicYuvOrg[layer], snrCSC, m_acListPicYuvRec[layer], outputAccessUnits, iPicIdInGOP, m_isTopFieldFirst);
					}
					else
					{
						m_apcTEncTop[layer]->encode_predict_online(Feature00[layer], Feature11[layer], Feature22[layer],
							model0[layer], model1[layer], model2[layer],
#if PUMode_FastDecision
							model0_PU[layer], model1_PU[layer], model2_PU[layer],
#endif
							M0[layer], M1[layer], M2[layer],
							frameSize0[layer], frameSize1[layer], frameSize2[layer], Feature_Num_Level0[layer], Feature_Num_Level1[layer], Feature_Num_Level2[layer],
#if Saliency_Weight
							img, img_output,
#endif
							flush ? 0 : pcPicYuvOrg[layer], snrCSC, m_acListPicYuvRec[layer], outputAccessUnits, iPicIdInGOP);
					}
#endif
				}

#else
				// call encoding function for one frame
				if (m_isField)
				{
					m_apcTEncTop[layer]->encode(flush ? 0 : pcPicYuvOrg[layer], snrCSC, m_acListPicYuvRec[layer], outputAccessUnits, iPicIdInGOP, m_isTopFieldFirst);
				}
				else
				{
					m_apcTEncTop[layer]->encode(flush ? 0 : pcPicYuvOrg[layer], snrCSC, m_acListPicYuvRec[layer], outputAccessUnits, iPicIdInGOP);
				}
#endif

			}
		}
#if R0247_SEI_ACTIVE
		if (bFirstFrame)
		{
			list<AccessUnit>::iterator first_au = outputAccessUnits.begin();
#if AVC_BASE
			if (m_nonHEVCBaseLayerFlag)
			{
				first_au++;
			}
#endif
			AccessUnit::iterator it_sps;
			for (it_sps = first_au->begin(); it_sps != first_au->end(); it_sps++)
			{
				if ((*it_sps)->m_nalUnitType == NAL_UNIT_SPS)
				{
					break;
				}
			}

			for (list<AccessUnit>::iterator it_au = ++outputAccessUnits.begin(); it_au != outputAccessUnits.end(); it_au++)
			{
				for (AccessUnit::iterator it_nalu = it_au->begin(); it_nalu != it_au->end(); it_nalu++)
				{
					if ((*it_nalu)->m_nalUnitType == NAL_UNIT_SPS)
					{
						it_sps = first_au->insert(++it_sps, *it_nalu);
						it_nalu = it_au->erase(it_nalu);
					}
				}
			}
		}
#endif

#if RC_SHVC_HARMONIZATION
		for (UInt layer = 0; layer < m_numLayers; layer++)
		{
			if (m_apcTEncTop[layer]->getUseRateCtrl())
			{
				m_apcTEncTop[layer]->getRateCtrl()->destroyRCGOP();
			}
		}
#endif

		iTotalNumEncoded = 0;
		for (UInt layer = 0; layer < m_numLayers; layer++)
		{
			//8
			// write bistream to file if necessary
			iNumEncoded = m_apcTEncTop[layer]->getNumPicRcvd();
			if (iNumEncoded > 0)
			{
				xWriteRecon(layer, iNumEncoded);
				iTotalNumEncoded += iNumEncoded;
			}
			m_apcTEncTop[layer]->setNumPicRcvd(0);  // 输出一个GOP后，把m_iNumPicRcvd设为0

			// temporally skip frames
			if (m_temporalSubsampleRatio > 1)
			{
				m_apcTVideoIOYuvInputFile[layer]->skipFrames(m_temporalSubsampleRatio - 1, m_apcTEncTop[layer]->getSourceWidth() - m_apcTEncTop[layer]->getPad(0), m_apcTEncTop[layer]->getSourceHeight() - m_apcTEncTop[layer]->getPad(1), m_apcLayerCfg[layer]->m_InputChromaFormatIDC);
			}
		}

		// write bitstream out
		if (iTotalNumEncoded)
		{
			if (bEos)
			{
				OutputNALUnit nalu(NAL_UNIT_EOB);
				nalu.m_nuhLayerId = 0;

				AccessUnit& accessUnit = outputAccessUnits.back();
				nalu.m_temporalId = 0;
				accessUnit.push_back(new NALUnitEBSP(nalu));
			}

			xWriteStream(bitstreamFile, iTotalNumEncoded, outputAccessUnits);
			outputAccessUnits.clear();
		}

		// print out summary
		if (bEos)
		{
			printOutSummary(m_isTopFieldFirst, m_printMSEBasedSequencePSNR, m_printSequenceMSE);
		}

		bFirstFrame = false;
	}
	// delete original YUV buffer
	for (UInt layer = 0; layer < m_numLayers; layer++)
	{
		pcPicYuvOrg[layer]->destroy();
		delete pcPicYuvOrg[layer];
		pcPicYuvOrg[layer] = NULL;

		// delete used buffers in encoder class
		m_apcTEncTop[layer]->deletePicBuffer();
		acPicYuvTrueOrg[layer].destroy();

#if Machine_Learning_Debug
		// 释放内存
		delete[] Feature0[layer], Feature1[layer], Feature2[layer], Feature00[layer], Feature11[layer], Feature22[layer],
			Truth0[layer], Truth1[layer], Truth2[layer], M0[layer], M1[layer], M2[layer],
			model0[layer], model1[layer], model2[layer];
#if PUMode_FastDecision
		delete[] Truth0_PU[layer], Truth1_PU[layer], Truth2_PU[layer], model0_PU[layer], model1_PU[layer], model2_PU[layer];
#endif
#if Offline
		delete[] paramater0[layer], paramater1[layer], paramater2[layer];
#endif
#endif

	}

#if Machine_Learning_Debug
	// 释放内存
	delete[] WIDTH, HEIGHT, frameSize0, frameSize1, frameSize2, W0, W1, W2, H0, H1, H2, training;
	delete[] Feature0, Feature1, Feature2, Feature00, Feature11, Feature22,
		Truth0, Truth1, Truth2, M0, M1, M2, model0, model1, model2;
#if PUMode_FastDecision
	delete[] Truth0_PU, Truth1_PU, Truth2_PU, model0_PU, model1_PU, model2_PU;
#endif
#if Offline
	delete[] paramater0, paramater1, paramater2;
#endif
#endif

	// delete buffers & classes
	xDeleteBuffer();
	xDestroyLib();

	printRateSummary();

	return;
}

Void TAppEncTop::printOutSummary(Bool isField, const Bool printMSEBasedSNR, const Bool printSequenceMSE)
{
	UInt layer;
	const Int rateMultiplier = isField ? 2 : 1;

	// set frame rate
	for (layer = 0; layer < m_numLayers; layer++)
	{
		m_apcTEncTop[layer]->getAnalyzeAll()->setFrmRate(m_apcLayerCfg[layer]->m_iFrameRate * rateMultiplier);
		m_apcTEncTop[layer]->getAnalyzeI()->setFrmRate(m_apcLayerCfg[layer]->m_iFrameRate * rateMultiplier);
		m_apcTEncTop[layer]->getAnalyzeP()->setFrmRate(m_apcLayerCfg[layer]->m_iFrameRate * rateMultiplier);
		m_apcTEncTop[layer]->getAnalyzeB()->setFrmRate(m_apcLayerCfg[layer]->m_iFrameRate * rateMultiplier);
	}

	//-- all
	printf("\n\nSUMMARY --------------------------------------------------------\n");
	for (layer = 0; layer < m_numLayers; layer++)
	{
		const UInt layerId = m_apcTEncTop[layer]->getVPS()->getLayerIdInNuh(layer);
		const BitDepths bitDepths(m_apcLayerCfg[layer]->m_internalBitDepth[CHANNEL_TYPE_LUMA], m_apcLayerCfg[layer]->m_internalBitDepth[CHANNEL_TYPE_CHROMA]);
		m_apcTEncTop[layer]->getAnalyzeAll()->printOut('a', m_apcLayerCfg[layer]->m_chromaFormatIDC, printMSEBasedSNR, printSequenceMSE, bitDepths, layerId);
	}

	printf("\n\nI Slices--------------------------------------------------------\n");
	for (layer = 0; layer < m_numLayers; layer++)
	{
		const UInt layerId = m_apcTEncTop[layer]->getVPS()->getLayerIdInNuh(layer);
		const BitDepths bitDepths(m_apcLayerCfg[layer]->m_internalBitDepth[CHANNEL_TYPE_LUMA], m_apcLayerCfg[layer]->m_internalBitDepth[CHANNEL_TYPE_CHROMA]);
		m_apcTEncTop[layer]->getAnalyzeI()->printOut('i', m_apcLayerCfg[layer]->m_chromaFormatIDC, printMSEBasedSNR, printSequenceMSE, bitDepths, layerId);
	}

	printf("\n\nP Slices--------------------------------------------------------\n");
	for (layer = 0; layer < m_numLayers; layer++)
	{
		const UInt layerId = m_apcTEncTop[layer]->getVPS()->getLayerIdInNuh(layer);
		const BitDepths bitDepths(m_apcLayerCfg[layer]->m_internalBitDepth[CHANNEL_TYPE_LUMA], m_apcLayerCfg[layer]->m_internalBitDepth[CHANNEL_TYPE_CHROMA]);
		m_apcTEncTop[layer]->getAnalyzeP()->printOut('p', m_apcLayerCfg[layer]->m_chromaFormatIDC, printMSEBasedSNR, printSequenceMSE, bitDepths, layerId);
	}

	printf("\n\nB Slices--------------------------------------------------------\n");
	for (layer = 0; layer < m_numLayers; layer++)
	{
		const UInt layerId = m_apcTEncTop[layer]->getVPS()->getLayerIdInNuh(layer);
		const BitDepths bitDepths(m_apcLayerCfg[layer]->m_internalBitDepth[CHANNEL_TYPE_LUMA], m_apcLayerCfg[layer]->m_internalBitDepth[CHANNEL_TYPE_CHROMA]);
		m_apcTEncTop[layer]->getAnalyzeB()->printOut('b', m_apcLayerCfg[layer]->m_chromaFormatIDC, printMSEBasedSNR, printSequenceMSE, bitDepths, layerId);
	}

	for (layer = 0; layer < m_numLayers; layer++)
	{
		if (!m_apcTEncTop[layer]->getSummaryOutFilename().empty())
		{
			const BitDepths bitDepths(m_apcLayerCfg[layer]->m_internalBitDepth[CHANNEL_TYPE_LUMA], m_apcLayerCfg[layer]->m_internalBitDepth[CHANNEL_TYPE_CHROMA]);

			m_apcTEncTop[layer]->getAnalyzeAll()->printSummary(m_apcLayerCfg[layer]->m_chromaFormatIDC, printSequenceMSE, bitDepths, m_apcTEncTop[layer]->getSummaryOutFilename());
		}
	}

	for (layer = 0; layer < m_numLayers; layer++)
	{
		if (!m_apcTEncTop[layer]->getSummaryPicFilenameBase().empty())
		{
			const BitDepths bitDepths(m_apcLayerCfg[layer]->m_internalBitDepth[CHANNEL_TYPE_LUMA], m_apcLayerCfg[layer]->m_internalBitDepth[CHANNEL_TYPE_CHROMA]);

			m_apcTEncTop[layer]->getAnalyzeI()->printSummary(m_apcLayerCfg[layer]->m_chromaFormatIDC, printSequenceMSE, bitDepths, m_apcTEncTop[layer]->getSummaryPicFilenameBase() + "I.txt");
			m_apcTEncTop[layer]->getAnalyzeP()->printSummary(m_apcLayerCfg[layer]->m_chromaFormatIDC, printSequenceMSE, bitDepths, m_apcTEncTop[layer]->getSummaryPicFilenameBase() + "P.txt");
			m_apcTEncTop[layer]->getAnalyzeB()->printSummary(m_apcLayerCfg[layer]->m_chromaFormatIDC, printSequenceMSE, bitDepths, m_apcTEncTop[layer]->getSummaryPicFilenameBase() + "B.txt");
		}
	}

	if (isField)
	{
		for (layer = 0; layer < m_numLayers; layer++)
		{
			const UInt layerId = m_apcTEncTop[layer]->getVPS()->getLayerIdInNuh(layer);
			const BitDepths bitDepths(m_apcLayerCfg[layer]->m_internalBitDepth[CHANNEL_TYPE_LUMA], m_apcLayerCfg[layer]->m_internalBitDepth[CHANNEL_TYPE_CHROMA]);
			TEncAnalyze *analyze = m_apcTEncTop[layer]->getAnalyzeAllin();

			//-- interlaced summary
			analyze->setFrmRate(m_apcLayerCfg[layer]->m_iFrameRate);
			analyze->setBits(m_apcTEncTop[layer]->getAnalyzeB()->getBits());
			// prior to the above statement, the interlace analyser does not contain the correct total number of bits.

			printf("\n\nSUMMARY INTERLACED ---------------------------------------------\n");
			analyze->printOut('a', m_apcLayerCfg[layer]->m_chromaFormatIDC, printMSEBasedSNR, printSequenceMSE, bitDepths, layerId);

			if (!m_apcTEncTop[layer]->getSummaryOutFilename().empty())
			{
				analyze->printSummary(m_apcLayerCfg[layer]->m_chromaFormatIDC, printSequenceMSE, bitDepths, m_apcTEncTop[layer]->getSummaryOutFilename());
			}
		}
	}

	printf("\n");
	for (layer = 0; layer < m_numLayers; layer++)
	{
		const UInt layerId = m_apcTEncTop[layer]->getVPS()->getLayerIdInNuh(layer);
		printf("RVM[L%d]: %.3lf\n", layerId, m_apcTEncTop[layer]->calculateRVM());
	}
	printf("\n");
}
#else
 // ====================================================================================================================
 // Public member functions
 // ====================================================================================================================

 /**
  - create internal class
  - initialize internal variable
  - until the end of input YUV file, call encoding function in TEncTop class
  - delete allocated buffers
  - destroy internal class
  .
  */
Void TAppEncTop::encode()
{
	fstream bitstreamFile(m_bitstreamFileName.c_str(), fstream::binary | fstream::out);
	if (!bitstreamFile)
	{
		fprintf(stderr, "\nfailed to open bitstream file `%s' for writing\n", m_bitstreamFileName.c_str());
		exit(EXIT_FAILURE);
	}

	TComPicYuv*       pcPicYuvOrg = new TComPicYuv;
	TComPicYuv*       pcPicYuvRec = NULL;

	// initialize internal class & member variables
	xInitLibCfg();
	xCreateLib();
	xInitLib(m_isField);

	printChromaFormat();

	// main encoder loop
	Int   iNumEncoded = 0;
	Bool  bEos = false;

	const InputColourSpaceConversion ipCSC = m_inputColourSpaceConvert;
	const InputColourSpaceConversion snrCSC = (!m_snrInternalColourSpace) ? m_inputColourSpaceConvert : IPCOLOURSPACE_UNCHANGED;

	list<AccessUnit> outputAccessUnits; ///< list of access units to write out.  is populated by the encoding process

	TComPicYuv cPicYuvTrueOrg;

	// allocate original YUV buffer
	if (m_isField)
	{
		pcPicYuvOrg->create(m_iSourceWidth, m_iSourceHeightOrg, m_chromaFormatIDC, m_uiMaxCUWidth, m_uiMaxCUHeight, m_uiMaxTotalCUDepth, true);
		cPicYuvTrueOrg.create(m_iSourceWidth, m_iSourceHeightOrg, m_chromaFormatIDC, m_uiMaxCUWidth, m_uiMaxCUHeight, m_uiMaxTotalCUDepth, true);
	}
	else
	{
		pcPicYuvOrg->create(m_iSourceWidth, m_iSourceHeight, m_chromaFormatIDC, m_uiMaxCUWidth, m_uiMaxCUHeight, m_uiMaxTotalCUDepth, true);
		cPicYuvTrueOrg.create(m_iSourceWidth, m_iSourceHeight, m_chromaFormatIDC, m_uiMaxCUWidth, m_uiMaxCUHeight, m_uiMaxTotalCUDepth, true);
	}

#if Machine_Learning_Debug //ZHULINWEI

	Int WIDTH = m_iSourceWidth;
	Int HEIGHT = m_iSourceHeight;

#if FULL_FEATURE
	Int Feature_Num_Level0 = 13, Feature_Num_Level1 = 13, Feature_Num_Level2 = 13;//the number of features
#endif
#if SELECTED_FEATURE
	Int Feature_Num_Level0 = 7, Feature_Num_Level1 = 4, Feature_Num_Level2 = 12;//the number of features
#endif
  //Int trainGOP = 1;
	Int frameSize0, frameSize1, frameSize2;
	//--------------------------边界考虑---------------------------------//
	if (WIDTH % 64 == 0 && HEIGHT % 64 == 0)
		frameSize0 = WIDTH / 64 * HEIGHT / 64;
	else if (WIDTH % 64 != 0 && HEIGHT % 64 == 0)
		frameSize0 = (WIDTH / 64 + 1)*HEIGHT / 64;
	else if (WIDTH % 64 == 0 && HEIGHT % 64 != 0)
		frameSize0 = WIDTH / 64 * (HEIGHT / 64 + 1);
	else if (WIDTH % 64 != 0 && HEIGHT % 64 != 0)
		frameSize0 = (WIDTH / 64 + 1)*(HEIGHT / 64 + 1);
	//------------------------------------------------------------------//
	frameSize1 = 4 * frameSize0; frameSize2 = 16 * frameSize0;
	Int W0, W1, W2, H0, H1, H2;

	if (WIDTH % 64 == 0)  W0 = WIDTH / 64;
	else              W0 = WIDTH / 64 + 1;

	W1 = 2 * W0; W2 = 4 * W0;

	if (HEIGHT % 64 == 0) H0 = HEIGHT / 64;
	else              H0 = HEIGHT / 64 + 1;

	H1 = 2 * H0; H2 = 4 * H0;

#if Saliency_Weight
	Mat img(Size(WIDTH, HEIGHT), CV_8UC3);
	Mat img_output(Size(WIDTH, HEIGHT), CV_8UC3);
#endif
	//------------------------------------------------------------------//
	Int trainframe = trainGOP * m_iGOPSize;
	//------------------------------------------------------------------//
	Double *Feature0 = new Double[trainframe*frameSize0*Feature_Num_Level0];
	Double *Feature1 = new Double[trainframe*frameSize1*Feature_Num_Level1];
	Double *Feature2 = new Double[trainframe*frameSize2*Feature_Num_Level2];
	memset(Feature0, 0, sizeof(Double)*trainframe*frameSize0*Feature_Num_Level0);
	memset(Feature1, 0, sizeof(Double)*trainframe*frameSize1*Feature_Num_Level1);
	memset(Feature2, 0, sizeof(Double)*trainframe*frameSize2*Feature_Num_Level2);


	Double *Feature00 = new Double[trainframe*frameSize0*Feature_Num_Level0];
	Double *Feature11 = new Double[trainframe*frameSize1*Feature_Num_Level1];
	Double *Feature22 = new Double[trainframe*frameSize2*Feature_Num_Level2];
	memset(Feature00, 0, sizeof(Double)*trainframe*frameSize0*Feature_Num_Level0);
	memset(Feature11, 0, sizeof(Double)*trainframe*frameSize1*Feature_Num_Level1);
	memset(Feature22, 0, sizeof(Double)*trainframe*frameSize2*Feature_Num_Level2);


	Int *Truth0 = new Int[trainframe*frameSize0];
	Int *Truth1 = new Int[trainframe*frameSize1];
	Int *Truth2 = new Int[trainframe*frameSize2];

#if PUMode_FastDecision
	Int *Truth0_PU = new Int[trainframe*frameSize0];
	Int *Truth1_PU = new Int[trainframe*frameSize1];
	Int *Truth2_PU = new Int[trainframe*frameSize2];
#endif

	maxmin *M0, *M1, *M2;
	M0 = new maxmin[Feature_Num_Level0];
	M1 = new maxmin[Feature_Num_Level1];
	M2 = new maxmin[Feature_Num_Level2];
#if Online
	svm_model *model0 = NULL, *model1 = NULL, *model2 = NULL;
#if PUMode_FastDecision
	svm_model *model0_PU = NULL, *model1_PU = NULL, *model2_PU = NULL;
#endif
#endif
	bool training = false;
	//------------------------------------------------------------------//
#if Offline
	svm_model *model0 = svm_load_model("model0.txt");
	svm_model *model1 = svm_load_model("model1.txt");
	svm_model *model2 = svm_load_model("model2.txt");
	svm_model *model3 = svm_load_model("model3.txt");

	FILE *paramater0 = fopen("par0.txt", "rb");
	FILE *paramater1 = fopen("par1.txt", "rb");
	FILE *paramater2 = fopen("par2.txt", "rb");

	char string_temp; int classifier[2]; int index0;
	//==============================================================================
	fseek(paramater0, 0, 0);
	fscanf(paramater0, "%c", &string_temp);
	fscanf(paramater0, "%d %d", &classifier[0], &classifier[1]);
	for (int k = 0; k < Feature_Num; k++)
	{
		fscanf(paramater0, "%d %lf %lf", &index0, &M0[k].minvalue, &M0[k].maxvalue);
	}
	//==============================================================================
	fseek(paramater1, 0, 0);
	fscanf(paramater1, "%c", &string_temp);
	fscanf(paramater1, "%d %d", &classifier[0], &classifier[1]);
	for (int k = 0; k < Feature_Num; k++)
	{
		fscanf(paramater1, "%d %lf %lf", &index0, &M1[k].minvalue, &M1[k].maxvalue);
	}
	//==============================================================================
	fseek(paramater2, 0, 0);
	fscanf(paramater2, "%c", &string_temp);
	fscanf(paramater2, "%d %d", &classifier[0], &classifier[1]);
	for (int k = 0; k < Feature_Num; k++)
	{
		fscanf(paramater2, "%d %lf %lf", &index0, &M2[k].minvalue, &M2[k].maxvalue);
	}
#endif

#endif

	while (!bEos)
	{
		// get buffers
		xGetBuffer(pcPicYuvRec);

		// read input YUV file
		m_cTVideoIOYuvInputFile.read(pcPicYuvOrg, &cPicYuvTrueOrg, ipCSC, m_aiPad, m_InputChromaFormatIDC, m_bClipInputVideoToRec709Range);

		// increase number of received frames
		m_iFrameRcvd++;

		bEos = (m_isField && (m_iFrameRcvd == (m_framesToBeEncoded >> 1))) || (!m_isField && (m_iFrameRcvd == m_framesToBeEncoded));

		Bool flush = 0;
		// if end of file (which is only detected on a read failure) flush the encoder of any queued pictures
		if (m_cTVideoIOYuvInputFile.isEof())
		{
			flush = true;
			bEos = true;
			m_iFrameRcvd--;
			m_cTEncTop.setFramesToBeEncoded(m_iFrameRcvd);
		}

		// call encoding function for one frame
#if Machine_Learning_Debug
#if Online
		if (training == true)
		{
			cout << " classifier generating........................................." << endl;
#if Online
			SVM_Train_Online(Feature0, Feature1, Feature2, Truth0, Truth1, Truth2,
#if PUMode_FastDecision
				Truth0_PU, Truth1_PU, Truth2_PU,
#endif
				M0, M1, M2,
				Feature_Num_Level0, Feature_Num_Level1, Feature_Num_Level2, trainframe, frameSize0, frameSize1, frameSize2, W0, W1, W2,
				WIDTH, HEIGHT, model0, model1, model2
#if PUMode_FastDecision
				, model0_PU[layer], model1_PU[layer], model2_PU[layer]
#endif
			);
#endif
			training = false;

			cout << " classifier finished............................................" << endl;
		}
#endif

		if (m_iFrameRcvd == 1)//Intra
		{
			if (m_isField)
			{
				m_cTEncTop.encode(bEos, flush ? 0 : pcPicYuvOrg, flush ? 0 : &cPicYuvTrueOrg, snrCSC, m_cListPicYuvRec, outputAccessUnits, iNumEncoded, m_isTopFieldFirst);
			}
			else
			{
				m_cTEncTop.encode(bEos, flush ? 0 : pcPicYuvOrg, flush ? 0 : &cPicYuvTrueOrg, snrCSC, m_cListPicYuvRec, outputAccessUnits, iNumEncoded);
			}
		}
#if Online
		else if (((m_iFrameRcvd - 2) / 4) % 25 == 0)//train   适用于LDP,对于RA,需要对应调整， 周期性更新模型，每隔100frames
		{
			if (m_isField)
			{
				m_cTEncTop.encode_train(Feature0, Feature1, Feature2,
					Truth0, Truth1, Truth2,
#if PUMode_FastDecision
					Truth0_PU, Truth1_PU, Truth2_PU,
#endif
					frameSize0, frameSize1, frameSize2, Feature_Num_Level0, Feature_Num_Level1, Feature_Num_Level2,
					bEos, flush ? 0 : pcPicYuvOrg, flush ? 0 : &cPicYuvTrueOrg, snrCSC, m_cListPicYuvRec, outputAccessUnits, iNumEncoded, m_isTopFieldFirst);
			}
			else
			{
				m_cTEncTop.encode_train(Feature0, Feature1, Feature2,
					Truth0, Truth1, Truth2,
#if PUMode_FastDecision
					Truth0_PU, Truth1_PU, Truth2_PU,
#endif
					frameSize0, frameSize1, frameSize2, Feature_Num_Level0, Feature_Num_Level1, Feature_Num_Level2,
					bEos, flush ? 0 : pcPicYuvOrg, flush ? 0 : &cPicYuvTrueOrg, snrCSC, m_cListPicYuvRec, outputAccessUnits, iNumEncoded);
			}
			if ((m_iFrameRcvd - 1) % 4 == 0) //适用于LDP,RA,需要对应调整
			{
				training = true;
			}
		}
#endif
		else //predict
		{
#if Online || Offline
			if (m_isField)
			{
				m_cTEncTop.encode_predict_online(Feature00, Feature11, Feature22,
					model0, model1, model2,
#if PUMode_FastDecision
					model0_PU, model1_PU, model2_PU,
#endif
					M0, M1, M2,
					frameSize0, frameSize1, frameSize2, Feature_Num_Level0, Feature_Num_Level1, Feature_Num_Level2,
#if Saliency_Weight
					img, img_output,
#endif
					bEos, flush ? 0 : pcPicYuvOrg, flush ? 0 : &cPicYuvTrueOrg, snrCSC, m_cListPicYuvRec, outputAccessUnits, iNumEncoded, m_isTopFieldFirst);
			}
			else
			{
				m_cTEncTop.encode_predict_online(Feature00, Feature11, Feature22,
					model0, model1, model2,
#if PUMode_FastDecision
					model0_PU, model1_PU, model2_PU,
#endif
					M0, M1, M2,
					frameSize0, frameSize1, frameSize2, Feature_Num_Level0, Feature_Num_Level1, Feature_Num_Level2,
#if Saliency_Weight
					img, img_output,
#endif
					bEos, flush ? 0 : pcPicYuvOrg, flush ? 0 : &cPicYuvTrueOrg, snrCSC, m_cListPicYuvRec, outputAccessUnits, iNumEncoded);
			}
#endif
		}

#else
		if (m_isField)
		{
			m_cTEncTop.encode(bEos, flush ? 0 : pcPicYuvOrg, flush ? 0 : &cPicYuvTrueOrg, snrCSC, m_cListPicYuvRec, outputAccessUnits, iNumEncoded, m_isTopFieldFirst);
		}
		else
		{
			m_cTEncTop.encode(bEos, flush ? 0 : pcPicYuvOrg, flush ? 0 : &cPicYuvTrueOrg, snrCSC, m_cListPicYuvRec, outputAccessUnits, iNumEncoded);
		}
#endif

		// write bistream to file if necessary
		if (iNumEncoded > 0)
		{
			xWriteOutput(bitstreamFile, iNumEncoded, outputAccessUnits);
			outputAccessUnits.clear();
		}
		// temporally skip frames
		if (m_temporalSubsampleRatio > 1)
		{
			m_cTVideoIOYuvInputFile.skipFrames(m_temporalSubsampleRatio - 1, m_iSourceWidth - m_aiPad[0], m_iSourceHeight - m_aiPad[1], m_InputChromaFormatIDC);
		}
	}

	m_cTEncTop.printSummary(m_isField);

	// delete original YUV buffer
	pcPicYuvOrg->destroy();
	delete pcPicYuvOrg;
	pcPicYuvOrg = NULL;

	// delete used buffers in encoder class
	m_cTEncTop.deletePicBuffer();
	cPicYuvTrueOrg.destroy();

	// delete buffers & classes
	xDeleteBuffer();
	xDestroyLib();

	printRateSummary();

	return;
}
#endif //SVC_EXTENSION

// ====================================================================================================================
// Protected member functions
// ====================================================================================================================

/**
 - application has picture buffer list with size of GOP
 - picture buffer list acts as ring buffer
 - end of the list has the latest picture
 .
 */
#if SVC_EXTENSION
Void TAppEncTop::xGetBuffer(TComPicYuv*& rpcPicYuvRec, UInt layer)
{
	assert(m_iGOPSize > 0);

	// org. buffer
	if (m_acListPicYuvRec[layer].size() >= (UInt)m_iGOPSize) // buffer will be 1 element longer when using field coding, to maintain first field whilst processing second.
	{
		rpcPicYuvRec = m_acListPicYuvRec[layer].popFront();
	}
	else
	{
		rpcPicYuvRec = new TComPicYuv;

		rpcPicYuvRec->create(m_apcLayerCfg[layer]->m_iSourceWidth, m_apcLayerCfg[layer]->m_iSourceHeight, m_apcLayerCfg[layer]->m_chromaFormatIDC, m_apcLayerCfg[layer]->m_uiMaxCUWidth, m_apcLayerCfg[layer]->m_uiMaxCUHeight, m_apcLayerCfg[layer]->m_uiMaxTotalCUDepth, true, NULL);
	}
	m_acListPicYuvRec[layer].pushBack(rpcPicYuvRec);
}

Void TAppEncTop::xDeleteBuffer()
{
	for (UInt layer = 0; layer < m_numLayers; layer++)
	{
		TComList<TComPicYuv*>::iterator iterPicYuvRec = m_acListPicYuvRec[layer].begin();

		Int iSize = Int(m_acListPicYuvRec[layer].size());

		for (Int i = 0; i < iSize; i++)
		{
			TComPicYuv*  pcPicYuvRec = *(iterPicYuvRec++);
			pcPicYuvRec->destroy();
			delete pcPicYuvRec; pcPicYuvRec = NULL;
		}
	}
}

// lxy 图像中的PU划分
#if Debug_PartitionInformation
#if Debug_PartitionAccuracy
Void TAppEncTop::PUPartInPic(TComPicYuv* pcPicYuvRec, TComDataCU *pcCtu, UChar* DepthCTU, SChar* PUType, Int *ContrastCUDepth, Int *ContrastPUType, SChar* PUPredMode, UInt* uiRefLayerId, UInt uiPelX, UInt uiPelY, UInt uiSize_X, UInt  uiSize_Y, UInt* uiPicSize, UChar uiDepth, UInt uiabsZIdxInCtu)
#else
Void TAppEncTop::PUPartInPic(TComPicYuv* pcPicYuvRec, UChar* DepthCTU, SChar* PUType, SChar* PUPredMode, UInt* uiRefLayerId, UInt uiPelX, UInt uiPelY, UInt uiSize_X, UInt  uiSize_Y, UInt* uiPicSize, UChar uiDepth)
#endif
{
	UInt uiNumPartition = 256 / (1 << (uiDepth << 1));
	UInt uiRPelX = 0, uiBPelY = 0; // lxy CU右下角像素坐标
	uiRPelX = uiPelX + uiSize_X - 1;
	uiBPelY = uiPelY + uiSize_Y - 1;
	if (DepthCTU[0] == uiDepth)
	{
		/*if (PUPredMode[0] != MODE_INTER) // lxy 只画使用帧间预测模式的PU
		{
			return;
		}*/


		/*
		// lxy 如果PU的参考图像位于基本层，则标为红色，位于增强层则标为蓝色
		//lxy uiPicSize存储图像的宽和高，因为CU超出图像范围时，不会继续划分为PU（xcompressCU函数），所以此处加一个判断
		if (uiRPelX >= uiPicSize[0] || uiBPelY >= uiPicSize[1])
		{
			return;
		}

		UChar iNumPart = 0;

		// lxy 判断CU划分后的PU数目
		switch (PUType[0])
		{
		  case SIZE_2Nx2N:    iNumPart = 1; break;
		  case SIZE_2NxN:     iNumPart = 2; break;
		  case SIZE_Nx2N:     iNumPart = 2; break;
		  case SIZE_NxN:      iNumPart = 4; break;
		  case SIZE_2NxnU:    iNumPart = 2; break;
		  case SIZE_2NxnD:    iNumPart = 2; break;
		  case SIZE_nLx2N:    iNumPart = 2; break;
		  case SIZE_nRx2N:    iNumPart = 2; break;
		  default:            assert(0);   break; // lxy 最后一行CTU有一部分在图像外，此时CU不继续划分为PU，所以没有存储PU的PartSize，导致这里断开
		}

		UInt  riWidthPU = 0, riHeightPU = 0; // lxy PU的宽和高
		UInt  uiPelXPU = 0, uiPelYPU = 0, uiRPelXPU = 0, uiBPelYPU = 0; // lxy PU左上角和右下角像素的坐标
		UInt  ruiPartAddr = 0; // lxy PU左上角的4x4块在CU中的Z扫描绝对地址
		for (Int iPartIdx = 0; iPartIdx < iNumPart; iPartIdx++)
		{
			switch (PUType[0])
			{
			  case SIZE_2NxN:
				riWidthPU = uiSize_X;      riHeightPU = uiSize_Y >> 1; ruiPartAddr = (iPartIdx == 0) ? 0 : uiNumPartition >> 1;
				uiPelXPU = uiPelX;
				uiPelYPU = (iPartIdx == 0) ? uiPelY : uiPelY + riHeightPU;
				uiRPelXPU = uiRPelX;
				uiBPelYPU = (iPartIdx == 0) ? uiBPelY - riHeightPU : uiBPelY;
				break;
			  case SIZE_Nx2N:
				riWidthPU = uiSize_X >> 1; riHeightPU = uiSize_Y;      ruiPartAddr = (iPartIdx == 0) ? 0 : uiNumPartition >> 2;
				uiPelXPU = (iPartIdx == 0) ? uiPelX : uiPelX + riWidthPU;
				uiPelYPU = uiPelY;
				uiRPelXPU = (iPartIdx == 0) ? uiRPelX - riWidthPU : uiRPelX;
				uiBPelYPU = uiBPelY;
				break;
			  case SIZE_NxN:
				riWidthPU = uiSize_X >> 1; riHeightPU = uiSize_Y >> 1; ruiPartAddr = (uiNumPartition >> 2) * iPartIdx;
				switch (iPartIdx)
				{
				  case 0:
					uiPelXPU = uiPelX;  uiPelYPU = uiPelY;  uiRPelXPU = uiRPelX - riWidthPU;  uiBPelYPU = uiBPelY - riHeightPU;
					break;
				  case 1:
					uiPelXPU = uiPelX + riWidthPU;  uiPelYPU = uiPelY;  uiRPelXPU = uiRPelX;  uiBPelYPU = uiBPelY - riHeightPU;
					break;
				  case 2:
					uiPelXPU = uiPelX;  uiPelYPU = uiPelY + riHeightPU;  uiRPelXPU = uiRPelX - riWidthPU;  uiBPelYPU = uiBPelY;
					break;
				  case 3:
					uiPelXPU = uiPelX + riWidthPU;  uiPelYPU = uiPelY + riHeightPU;  uiRPelXPU = uiRPelX;  uiBPelYPU = uiBPelY;
					break;
				  default:
					uiPelXPU = uiPelX;  uiPelYPU = uiPelY;  uiRPelXPU = uiRPelX;  uiBPelYPU = uiBPelY; // lxy 不会过缺省的情况
					break;
				}
				break;
			  case SIZE_2NxnU:
				riWidthPU = uiSize_X;
				riHeightPU = (iPartIdx == 0) ? uiSize_Y >> 2 : (uiSize_Y >> 2) + (uiSize_Y >> 1);
				ruiPartAddr = (iPartIdx == 0) ? 0 : uiNumPartition >> 3;
				uiPelXPU = uiPelX;
				uiPelYPU = (iPartIdx == 0) ? uiPelY : uiPelY + (uiSize_Y - riHeightPU);
				uiRPelXPU = uiRPelX;
				uiBPelYPU = (iPartIdx == 0) ? uiBPelY - (uiSize_Y - riHeightPU) : uiBPelY;
				break;
			  case SIZE_2NxnD:
				riWidthPU = uiSize_X;
				riHeightPU = (iPartIdx == 0) ? (uiSize_Y >> 2) + (uiSize_Y >> 1) : uiSize_Y >> 2;
				ruiPartAddr = (iPartIdx == 0) ? 0 : (uiNumPartition >> 1) + (uiNumPartition >> 3);
				uiPelXPU = uiPelX;
				uiPelYPU = (iPartIdx == 0) ? uiPelY : uiPelY + (uiSize_Y - riHeightPU);
				uiRPelXPU = uiRPelX;
				uiBPelYPU = (iPartIdx == 0) ? uiBPelY - (uiSize_Y - riHeightPU) : uiBPelY;
				break;
			  case SIZE_nLx2N:
				riWidthPU = (iPartIdx == 0) ? uiSize_X >> 2 : (uiSize_X >> 2) + (uiSize_X >> 1);
				riHeightPU = uiSize_Y;
				ruiPartAddr = (iPartIdx == 0) ? 0 : uiNumPartition >> 4;
				uiPelXPU = (iPartIdx == 0) ? uiPelX : uiPelX + (uiSize_X - riWidthPU);
				uiPelYPU = uiPelY;
				uiRPelXPU = (iPartIdx == 0) ? uiRPelX - (uiSize_X - riWidthPU) : uiRPelX;
				uiBPelYPU = uiBPelY;
				break;
			  case SIZE_nRx2N:
				riWidthPU = (iPartIdx == 0) ? (uiSize_X >> 2) + (uiSize_X >> 1) : uiSize_X >> 2;
				riHeightPU = uiSize_Y;
				ruiPartAddr = (iPartIdx == 0) ? 0 : (uiNumPartition >> 2) + (uiNumPartition >> 4);
				uiPelXPU = (iPartIdx == 0) ? uiPelX : uiPelX + (uiSize_X - riWidthPU);
				uiPelYPU = uiPelY;
				uiRPelXPU = (iPartIdx == 0) ? uiRPelX - (uiSize_X - riWidthPU) : uiRPelX;
				uiBPelYPU = uiBPelY;
				break;
			  default:
				assert(PUType[0] == SIZE_2Nx2N);
				riWidthPU = uiSize_X;      riHeightPU = uiSize_Y;      ruiPartAddr = 0;
				uiPelXPU = uiPelX;        uiPelYPU = uiPelY;        uiRPelXPU = uiRPelX;        uiBPelYPU = uiBPelY;
				break;
			}

			for (UInt comp = 0; comp < pcPicYuvRec->getNumberValidComponents(); comp++) // lxy 画出PU的划分线
			{
				const ComponentID compID = ComponentID(comp);
				const UInt width = pcPicYuvRec->getWidth(compID); // lxy width和height是原始图像的宽和高
				const UInt height = pcPicYuvRec->getHeight(compID);
				Pel *PicPel = pcPicYuvRec->getAddr(compID); // lxy 得到每个颜色分量的值
				const UInt PicStride = pcPicYuvRec->getStride(compID); // lxy stride原始图像的基础上添加了填充部分
				if (comp == 0)
				{
					for (UInt i = 0; i < riWidthPU; i++)
					{
						UInt PelIdx;
						// PelIdx = uiPelYPU * PicStride + uiPelXPU + i; // lxy 画出上下两条边
						// PicPel[PelIdx] = uiRefLayerId[ruiPartAddr] ? 41 : 82; // lxy 判断参考图像位于基本层（红色）还是增强层（蓝色）
						if (uiPelYPU == 0)  // 图像上边界（避免划分线重复，除了边界处，只画下边和右边）
						{
							PelIdx = uiPelYPU * PicStride + uiPelXPU + i;  // 画出上边
							PicPel[PelIdx] = uiRefLayerId[ruiPartAddr] ? 41 : 82; // lxy 判断参考图像位于基本层（红色）还是增强层（蓝色）
						}
						PelIdx = uiBPelYPU * PicStride + uiPelXPU + i;  // 画出下边
						PicPel[PelIdx] = uiRefLayerId[ruiPartAddr] ? 41 : 82;
					}
					for (UInt i = 0; i < riHeightPU; i++)
					{
						UInt PelIdx;
						// PelIdx = (uiPelYPU + i) * PicStride + uiPelXPU; // lxy 画出左右两条边
						// PicPel[PelIdx] = uiRefLayerId[ruiPartAddr] ? 41 : 82;
						if (uiPelXPU == 0)  // 图像左边界（避免划分线重复，除了边界处，只画下边和右边）
						{
							PelIdx = (uiPelYPU + i) * PicStride + uiPelXPU;  // 画出左边
							PicPel[PelIdx] = uiRefLayerId[ruiPartAddr] ? 41 : 82;
						}
						PelIdx = (uiPelYPU + i) * PicStride + uiRPelXPU;  // 画出右边
						PicPel[PelIdx] = uiRefLayerId[ruiPartAddr] ? 41 : 82;
					}
				}
				else if (comp == 1) // lxy U分量
				{
					UInt uiPelXPU_U = uiPelXPU / 2;
					UInt uiPelYPU_U = uiPelYPU / 2;
					UInt riWidthPU_U = riWidthPU / 2;
					UInt riHeightPU_U = riHeightPU / 2;
					UInt uiRPelXPU_U = uiRPelXPU / 2;
					UInt uiBPelYPU_U = uiBPelYPU / 2;
					for (UInt i = 0; i < riWidthPU_U; i++)
					{
						UInt PelIdx;
						// PelIdx = uiPelYPU_U * PicStride + uiPelXPU_U + i; // lxy 画出上下两条边
						// PicPel[PelIdx] = uiRefLayerId[ruiPartAddr] ? 240 : 90;
						if (uiPelYPU_U == 0)  // 图像上边界
						{
							PelIdx = uiPelYPU_U * PicStride + uiPelXPU_U + i;  // 画出上边
							PicPel[PelIdx] = uiRefLayerId[ruiPartAddr] ? 240 : 90;
						}
						PelIdx = uiBPelYPU_U * PicStride + uiPelXPU_U + i;  // 画出下边
						PicPel[PelIdx] = uiRefLayerId[ruiPartAddr] ? 240 : 90;
					}
					for (UInt i = 0; i < riHeightPU_U; i++)
					{
						UInt PelIdx;
						// PelIdx = (uiPelYPU_U + i) * PicStride + uiPelXPU_U; // lxy 画出左右两条边
						// PicPel[PelIdx] = uiRefLayerId[ruiPartAddr] ? 240 : 90;
						if (uiPelXPU_U == 0)  // 图像左边界
						{
							PelIdx = (uiPelYPU_U + i) * PicStride + uiPelXPU_U;  // 画出左边
							PicPel[PelIdx] = uiRefLayerId[ruiPartAddr] ? 240 : 90;
						}
						PelIdx = (uiPelYPU_U + i) * PicStride + uiRPelXPU_U;  // 画出右边
						PicPel[PelIdx] = uiRefLayerId[ruiPartAddr] ? 240 : 90;
					}
				}
				else // lxy V分量
				{
					UInt uiPelXPU_V = uiPelXPU / 2; // lxy 注意：不要再U分量坐标的基础上改变V分量的坐标
					UInt uiPelYPU_V = uiPelYPU / 2;
					UInt riWidthPU_V = riWidthPU / 2;
					UInt riHeightPU_V = riHeightPU / 2;
					UInt uiRPelXPU_V = uiRPelXPU / 2;
					UInt uiBPelYPU_V = uiBPelYPU / 2;
					for (UInt i = 0; i < riWidthPU_V; i++)
					{
						UInt PelIdx;
						// PelIdx = uiPelYPU_V * PicStride + uiPelXPU_V + i; // lxy 画出上下两条边
						// PicPel[PelIdx] = uiRefLayerId[ruiPartAddr] ? 110 : 240;
						if (uiPelYPU_V == 0)  // 图像上边界
						{
							PelIdx = uiPelYPU_V * PicStride + uiPelXPU_V + i;  // 画出上边
							PicPel[PelIdx] = uiRefLayerId[ruiPartAddr] ? 110 : 240;
						}
						PelIdx = uiBPelYPU_V * PicStride + uiPelXPU_V + i;  // 画出下边
						PicPel[PelIdx] = uiRefLayerId[ruiPartAddr] ? 110 : 240;
					}
					for (UInt i = 0; i < riHeightPU_V; i++)
					{
						UInt PelIdx;
						// PelIdx = (uiPelYPU_V + i) * PicStride + uiPelXPU_V; // lxy 画出左右两条边
						// PicPel[PelIdx] = uiRefLayerId[ruiPartAddr] ? 110 : 240;
						if (uiPelXPU_V == 0)  // 图像左边界
						{
							PelIdx = (uiPelYPU_V + i) * PicStride + uiPelXPU_V;  // 画出左边
							PicPel[PelIdx] = uiRefLayerId[ruiPartAddr] ? 110 : 240;
						}
						PelIdx = (uiPelYPU_V + i) * PicStride + uiRPelXPU_V;  // 画出右边
						PicPel[PelIdx] = uiRefLayerId[ruiPartAddr] ? 110 : 240;
					}
				}
			}
		}
		return; // lxy CU中的PU全部画完 */



#if Debug_PartitionAccuracy
		// lxy 绘制所有的PU（蓝色）
		// lxy uiPicSize存储图像的宽和高，因为CU超出图像范围时，不会继续划分为PU（xcompressCU函数），所以此处加一个判断
		if (uiRPelX >= uiPicSize[0] || uiBPelY >= uiPicSize[1])
		{
			return;
		}

		UChar iNumPart = 0;

		// lxy 判断CU划分后的PU数目
		switch (PUType[0])
		{
		case SIZE_2Nx2N:    iNumPart = 1; break;
		case SIZE_2NxN:     iNumPart = 2; break;
		case SIZE_Nx2N:     iNumPart = 2; break;
		case SIZE_NxN:      iNumPart = 4; break;
		case SIZE_2NxnU:    iNumPart = 2; break;
		case SIZE_2NxnD:    iNumPart = 2; break;
		case SIZE_nLx2N:    iNumPart = 2; break;
		case SIZE_nRx2N:    iNumPart = 2; break;
		default:            assert(0);   break; // lxy 最后一行CTU有一部分在图像外，此时CU不继续划分为PU，所以没有存储PU的PartSize，导致这里断开
		}

		UInt  riWidthPU = 0, riHeightPU = 0; // lxy PU的宽和高
		UInt  uiPelXPU = 0, uiPelYPU = 0, uiRPelXPU = 0, uiBPelYPU = 0; // lxy PU左上角和右下角像素的坐标
		UInt  ruiPartAddr = 0; // lxy PU左上角的4x4块在CU中的Z扫描绝对地址
		if ((pcCtu->getPic()->getPOC() == 5) && (pcCtu->getPic()->getLayerId()))  // 比较增强层第5帧
		{
			// CU划分深度和PU预测模式都相同
			Bool bEqualType = ((ContrastCUDepth[uiabsZIdxInCtu] == DepthCTU[0]) && (ContrastPUType[uiabsZIdxInCtu] == PUType[0]));

			for (Int iPartIdx = 0; iPartIdx < iNumPart; iPartIdx++)
			{
				switch (PUType[0])
				{
				case SIZE_2NxN:
					riWidthPU = uiSize_X;      riHeightPU = uiSize_Y >> 1; ruiPartAddr = (iPartIdx == 0) ? 0 : uiNumPartition >> 1;
					uiPelXPU = uiPelX;
					uiPelYPU = (iPartIdx == 0) ? uiPelY : uiPelY + riHeightPU;
					uiRPelXPU = uiRPelX;
					uiBPelYPU = (iPartIdx == 0) ? uiBPelY - riHeightPU : uiBPelY;
					break;
				case SIZE_Nx2N:
					riWidthPU = uiSize_X >> 1; riHeightPU = uiSize_Y;      ruiPartAddr = (iPartIdx == 0) ? 0 : uiNumPartition >> 2;
					uiPelXPU = (iPartIdx == 0) ? uiPelX : uiPelX + riWidthPU;
					uiPelYPU = uiPelY;
					uiRPelXPU = (iPartIdx == 0) ? uiRPelX - riWidthPU : uiRPelX;
					uiBPelYPU = uiBPelY;
					break;
				case SIZE_NxN:
					riWidthPU = uiSize_X >> 1; riHeightPU = uiSize_Y >> 1; ruiPartAddr = (uiNumPartition >> 2) * iPartIdx;
					switch (iPartIdx)
					{
					case 0:
						uiPelXPU = uiPelX;  uiPelYPU = uiPelY;  uiRPelXPU = uiRPelX - riWidthPU;  uiBPelYPU = uiBPelY - riHeightPU;
						break;
					case 1:
						uiPelXPU = uiPelX + riWidthPU;  uiPelYPU = uiPelY;  uiRPelXPU = uiRPelX;  uiBPelYPU = uiBPelY - riHeightPU;
						break;
					case 2:
						uiPelXPU = uiPelX;  uiPelYPU = uiPelY + riHeightPU;  uiRPelXPU = uiRPelX - riWidthPU;  uiBPelYPU = uiBPelY;
						break;
					case 3:
						uiPelXPU = uiPelX + riWidthPU;  uiPelYPU = uiPelY + riHeightPU;  uiRPelXPU = uiRPelX;  uiBPelYPU = uiBPelY;
						break;
					default:
						uiPelXPU = uiPelX;  uiPelYPU = uiPelY;  uiRPelXPU = uiRPelX;  uiBPelYPU = uiBPelY; // lxy 不会过缺省的情况
						break;
					}
					break;
				case SIZE_2NxnU:
					riWidthPU = uiSize_X;
					riHeightPU = (iPartIdx == 0) ? uiSize_Y >> 2 : (uiSize_Y >> 2) + (uiSize_Y >> 1);
					ruiPartAddr = (iPartIdx == 0) ? 0 : uiNumPartition >> 3;
					uiPelXPU = uiPelX;
					uiPelYPU = (iPartIdx == 0) ? uiPelY : uiPelY + (uiSize_Y - riHeightPU);
					uiRPelXPU = uiRPelX;
					uiBPelYPU = (iPartIdx == 0) ? uiBPelY - (uiSize_Y - riHeightPU) : uiBPelY;
					break;
				case SIZE_2NxnD:
					riWidthPU = uiSize_X;
					riHeightPU = (iPartIdx == 0) ? (uiSize_Y >> 2) + (uiSize_Y >> 1) : uiSize_Y >> 2;
					ruiPartAddr = (iPartIdx == 0) ? 0 : (uiNumPartition >> 1) + (uiNumPartition >> 3);
					uiPelXPU = uiPelX;
					uiPelYPU = (iPartIdx == 0) ? uiPelY : uiPelY + (uiSize_Y - riHeightPU);
					uiRPelXPU = uiRPelX;
					uiBPelYPU = (iPartIdx == 0) ? uiBPelY - (uiSize_Y - riHeightPU) : uiBPelY;
					break;
				case SIZE_nLx2N:
					riWidthPU = (iPartIdx == 0) ? uiSize_X >> 2 : (uiSize_X >> 2) + (uiSize_X >> 1);
					riHeightPU = uiSize_Y;
					ruiPartAddr = (iPartIdx == 0) ? 0 : uiNumPartition >> 4;
					uiPelXPU = (iPartIdx == 0) ? uiPelX : uiPelX + (uiSize_X - riWidthPU);
					uiPelYPU = uiPelY;
					uiRPelXPU = (iPartIdx == 0) ? uiRPelX - (uiSize_X - riWidthPU) : uiRPelX;
					uiBPelYPU = uiBPelY;
					break;
				case SIZE_nRx2N:
					riWidthPU = (iPartIdx == 0) ? (uiSize_X >> 2) + (uiSize_X >> 1) : uiSize_X >> 2;
					riHeightPU = uiSize_Y;
					ruiPartAddr = (iPartIdx == 0) ? 0 : (uiNumPartition >> 2) + (uiNumPartition >> 4);
					uiPelXPU = (iPartIdx == 0) ? uiPelX : uiPelX + (uiSize_X - riWidthPU);
					uiPelYPU = uiPelY;
					uiRPelXPU = (iPartIdx == 0) ? uiRPelX - (uiSize_X - riWidthPU) : uiRPelX;
					uiBPelYPU = uiBPelY;
					break;
				default:
					assert(PUType[0] == SIZE_2Nx2N);
					riWidthPU = uiSize_X;      riHeightPU = uiSize_Y;      ruiPartAddr = 0;
					uiPelXPU = uiPelX;        uiPelYPU = uiPelY;        uiRPelXPU = uiRPelX;        uiBPelYPU = uiBPelY;
					break;
				}

				for (UInt comp = 0; comp < pcPicYuvRec->getNumberValidComponents(); comp++) // lxy 画出PU的划分线
				{
					const ComponentID compID = ComponentID(comp);
					const UInt width = pcPicYuvRec->getWidth(compID); // lxy width和height是原始图像的宽和高
					const UInt height = pcPicYuvRec->getHeight(compID);
					Pel *PicPel = pcPicYuvRec->getAddr(compID); // lxy 得到每个颜色分量的值
					const UInt PicStride = pcPicYuvRec->getStride(compID); // lxy stride原始图像的基础上添加了填充部分
					if (comp == 0)
					{
						for (UInt i = 0; i < riWidthPU; i++)
						{
							UInt PelIdx;
							PelIdx = uiPelYPU * PicStride + uiPelXPU + i; // lxy 画出上下两条边
							PicPel[PelIdx] = bEqualType ? 41 : 82;
							//if (uiPelYPU == 0)  // 图像上边界（避免划分线重复，除了边界处，只画下边和右边）
							//{
							//	PelIdx = uiPelYPU * PicStride + uiPelXPU + i;  // 画出上边
							//	PicPel[PelIdx] = bEqualType ? 41 : 82;
							//}
							PelIdx = uiBPelYPU * PicStride + uiPelXPU + i;  // 画出下边
							PicPel[PelIdx] = bEqualType ? 41 : 82;
						}
						for (UInt i = 0; i < riHeightPU; i++)
						{
							UInt PelIdx;
							PelIdx = (uiPelYPU + i) * PicStride + uiPelXPU; // lxy 画出左右两条边
							PicPel[PelIdx] = bEqualType ? 41 : 82;
							//if (uiPelXPU == 0)  // 图像左边界（避免划分线重复，除了边界处，只画下边和右边）
							//{
							//	PelIdx = (uiPelYPU + i) * PicStride + uiPelXPU;  // 画出左边
							//	PicPel[PelIdx] = bEqualType ? 41 : 82;
							//}
							PelIdx = (uiPelYPU + i) * PicStride + uiRPelXPU;  // 画出右边
							PicPel[PelIdx] = bEqualType ? 41 : 82;
						}
					}
					else if (comp == 1) // lxy U分量
					{
						UInt uiPelXPU_U = uiPelXPU / 2;
						UInt uiPelYPU_U = uiPelYPU / 2;
						UInt riWidthPU_U = riWidthPU / 2;
						UInt riHeightPU_U = riHeightPU / 2;
						UInt uiRPelXPU_U = uiRPelXPU / 2;
						UInt uiBPelYPU_U = uiBPelYPU / 2;
						for (UInt i = 0; i < riWidthPU_U; i++)
						{
							UInt PelIdx;
							PelIdx = uiPelYPU_U * PicStride + uiPelXPU_U + i; // lxy 画出上下两条边
							PicPel[PelIdx] = bEqualType ? 240 : 90;
							//if (uiPelYPU_U == 0)  // 图像上边界
							//{
							//	PelIdx = uiPelYPU_U * PicStride + uiPelXPU_U + i;  // 画出上边
							//	PicPel[PelIdx] = bEqualType ? 240 : 90;
							//}
							PelIdx = uiBPelYPU_U * PicStride + uiPelXPU_U + i;  // 画出下边
							PicPel[PelIdx] = bEqualType ? 240 : 90;
						}
						for (UInt i = 0; i < riHeightPU_U; i++)
						{
							UInt PelIdx;
							PelIdx = (uiPelYPU_U + i) * PicStride + uiPelXPU_U; // lxy 画出左右两条边
							PicPel[PelIdx] = bEqualType ? 240 : 90;
							//if (uiPelXPU_U == 0)  // 图像左边界
							//{
							//	PelIdx = (uiPelYPU_U + i) * PicStride + uiPelXPU_U;  // 画出左边
							//	PicPel[PelIdx] = bEqualType ? 240 : 90;
							//}
							PelIdx = (uiPelYPU_U + i) * PicStride + uiRPelXPU_U;  // 画出右边
							PicPel[PelIdx] = bEqualType ? 240 : 90;
						}
					}
					else // lxy V分量
					{
						UInt uiPelXPU_V = uiPelXPU / 2; // lxy 注意：不要再U分量坐标的基础上改变V分量的坐标
						UInt uiPelYPU_V = uiPelYPU / 2;
						UInt riWidthPU_V = riWidthPU / 2;
						UInt riHeightPU_V = riHeightPU / 2;
						UInt uiRPelXPU_V = uiRPelXPU / 2;
						UInt uiBPelYPU_V = uiBPelYPU / 2;
						for (UInt i = 0; i < riWidthPU_V; i++)
						{
							UInt PelIdx;
							PelIdx = uiPelYPU_V * PicStride + uiPelXPU_V + i; // lxy 画出上下两条边
							PicPel[PelIdx] = bEqualType ? 110 : 240;
							//if (uiPelYPU_V == 0)  // 图像上边界
							//{
							//	PelIdx = uiPelYPU_V * PicStride + uiPelXPU_V + i;  // 画出上边
							//	PicPel[PelIdx] = bEqualType ? 110 : 240;
							//}
							PelIdx = uiBPelYPU_V * PicStride + uiPelXPU_V + i;  // 画出下边
							PicPel[PelIdx] = bEqualType ? 110 : 240;
						}
						for (UInt i = 0; i < riHeightPU_V; i++)
						{
							UInt PelIdx;
							PelIdx = (uiPelYPU_V + i) * PicStride + uiPelXPU_V; // lxy 画出左右两条边
							PicPel[PelIdx] = bEqualType ? 110 : 240;
							//if (uiPelXPU_V == 0)  // 图像左边界
							//{
							//	PelIdx = (uiPelYPU_V + i) * PicStride + uiPelXPU_V;  // 画出左边
							//	PicPel[PelIdx] = bEqualType ? 110 : 240;
							//}
							PelIdx = (uiPelYPU_V + i) * PicStride + uiRPelXPU_V;  // 画出右边
							PicPel[PelIdx] = bEqualType ? 110 : 240;
						}
					}
				}
			}
			return; // lxy CU中的PU全部画完

		}
		else
		{

			for (Int iPartIdx = 0; iPartIdx < iNumPart; iPartIdx++)
			{
				switch (PUType[0])
				{
				case SIZE_2NxN:
					riWidthPU = uiSize_X;      riHeightPU = uiSize_Y >> 1; ruiPartAddr = (iPartIdx == 0) ? 0 : uiNumPartition >> 1;
					uiPelXPU = uiPelX;
					uiPelYPU = (iPartIdx == 0) ? uiPelY : uiPelY + riHeightPU;
					uiRPelXPU = uiRPelX;
					uiBPelYPU = (iPartIdx == 0) ? uiBPelY - riHeightPU : uiBPelY;
					break;
				case SIZE_Nx2N:
					riWidthPU = uiSize_X >> 1; riHeightPU = uiSize_Y;      ruiPartAddr = (iPartIdx == 0) ? 0 : uiNumPartition >> 2;
					uiPelXPU = (iPartIdx == 0) ? uiPelX : uiPelX + riWidthPU;
					uiPelYPU = uiPelY;
					uiRPelXPU = (iPartIdx == 0) ? uiRPelX - riWidthPU : uiRPelX;
					uiBPelYPU = uiBPelY;
					break;
				case SIZE_NxN:
					riWidthPU = uiSize_X >> 1; riHeightPU = uiSize_Y >> 1; ruiPartAddr = (uiNumPartition >> 2) * iPartIdx;
					switch (iPartIdx)
					{
					case 0:
						uiPelXPU = uiPelX;  uiPelYPU = uiPelY;  uiRPelXPU = uiRPelX - riWidthPU;  uiBPelYPU = uiBPelY - riHeightPU;
						break;
					case 1:
						uiPelXPU = uiPelX + riWidthPU;  uiPelYPU = uiPelY;  uiRPelXPU = uiRPelX;  uiBPelYPU = uiBPelY - riHeightPU;
						break;
					case 2:
						uiPelXPU = uiPelX;  uiPelYPU = uiPelY + riHeightPU;  uiRPelXPU = uiRPelX - riWidthPU;  uiBPelYPU = uiBPelY;
						break;
					case 3:
						uiPelXPU = uiPelX + riWidthPU;  uiPelYPU = uiPelY + riHeightPU;  uiRPelXPU = uiRPelX;  uiBPelYPU = uiBPelY;
						break;
					default:
						uiPelXPU = uiPelX;  uiPelYPU = uiPelY;  uiRPelXPU = uiRPelX;  uiBPelYPU = uiBPelY; // lxy 不会过缺省的情况
						break;
					}
					break;
				case SIZE_2NxnU:
					riWidthPU = uiSize_X;
					riHeightPU = (iPartIdx == 0) ? uiSize_Y >> 2 : (uiSize_Y >> 2) + (uiSize_Y >> 1);
					ruiPartAddr = (iPartIdx == 0) ? 0 : uiNumPartition >> 3;
					uiPelXPU = uiPelX;
					uiPelYPU = (iPartIdx == 0) ? uiPelY : uiPelY + (uiSize_Y - riHeightPU);
					uiRPelXPU = uiRPelX;
					uiBPelYPU = (iPartIdx == 0) ? uiBPelY - (uiSize_Y - riHeightPU) : uiBPelY;
					break;
				case SIZE_2NxnD:
					riWidthPU = uiSize_X;
					riHeightPU = (iPartIdx == 0) ? (uiSize_Y >> 2) + (uiSize_Y >> 1) : uiSize_Y >> 2;
					ruiPartAddr = (iPartIdx == 0) ? 0 : (uiNumPartition >> 1) + (uiNumPartition >> 3);
					uiPelXPU = uiPelX;
					uiPelYPU = (iPartIdx == 0) ? uiPelY : uiPelY + (uiSize_Y - riHeightPU);
					uiRPelXPU = uiRPelX;
					uiBPelYPU = (iPartIdx == 0) ? uiBPelY - (uiSize_Y - riHeightPU) : uiBPelY;
					break;
				case SIZE_nLx2N:
					riWidthPU = (iPartIdx == 0) ? uiSize_X >> 2 : (uiSize_X >> 2) + (uiSize_X >> 1);
					riHeightPU = uiSize_Y;
					ruiPartAddr = (iPartIdx == 0) ? 0 : uiNumPartition >> 4;
					uiPelXPU = (iPartIdx == 0) ? uiPelX : uiPelX + (uiSize_X - riWidthPU);
					uiPelYPU = uiPelY;
					uiRPelXPU = (iPartIdx == 0) ? uiRPelX - (uiSize_X - riWidthPU) : uiRPelX;
					uiBPelYPU = uiBPelY;
					break;
				case SIZE_nRx2N:
					riWidthPU = (iPartIdx == 0) ? (uiSize_X >> 2) + (uiSize_X >> 1) : uiSize_X >> 2;
					riHeightPU = uiSize_Y;
					ruiPartAddr = (iPartIdx == 0) ? 0 : (uiNumPartition >> 2) + (uiNumPartition >> 4);
					uiPelXPU = (iPartIdx == 0) ? uiPelX : uiPelX + (uiSize_X - riWidthPU);
					uiPelYPU = uiPelY;
					uiRPelXPU = (iPartIdx == 0) ? uiRPelX - (uiSize_X - riWidthPU) : uiRPelX;
					uiBPelYPU = uiBPelY;
					break;
				default:
					assert(PUType[0] == SIZE_2Nx2N);
					riWidthPU = uiSize_X;      riHeightPU = uiSize_Y;      ruiPartAddr = 0;
					uiPelXPU = uiPelX;        uiPelYPU = uiPelY;        uiRPelXPU = uiRPelX;        uiBPelYPU = uiBPelY;
					break;
				}

				for (UInt comp = 0; comp < pcPicYuvRec->getNumberValidComponents(); comp++) // lxy 画出PU的划分线
				{
					const ComponentID compID = ComponentID(comp);
					const UInt width = pcPicYuvRec->getWidth(compID); // lxy width和height是原始图像的宽和高
					const UInt height = pcPicYuvRec->getHeight(compID);
					Pel *PicPel = pcPicYuvRec->getAddr(compID); // lxy 得到每个颜色分量的值
					const UInt PicStride = pcPicYuvRec->getStride(compID); // lxy stride原始图像的基础上添加了填充部分
					if (comp == 0)
					{
						for (UInt i = 0; i < riWidthPU; i++)
						{
							UInt PelIdx;
							// PelIdx = uiPelYPU * PicStride + uiPelXPU + i; // lxy 画出上下两条边
							// PicPel[PelIdx] = 41;
							if (uiPelYPU == 0)  // 图像上边界（避免划分线重复，除了边界处，只画下边和右边）
							{
								PelIdx = uiPelYPU * PicStride + uiPelXPU + i;  // 画出上边
								PicPel[PelIdx] = 41;
							}
							PelIdx = uiBPelYPU * PicStride + uiPelXPU + i;  // 画出下边
							PicPel[PelIdx] = 41;
						}
						for (UInt i = 0; i < riHeightPU; i++)
						{
							UInt PelIdx;
							// PelIdx = (uiPelYPU + i) * PicStride + uiPelXPU; // lxy 画出左右两条边
							// PicPel[PelIdx] = 41;
							if (uiPelXPU == 0)  // 图像左边界（避免划分线重复，除了边界处，只画下边和右边）
							{
								PelIdx = (uiPelYPU + i) * PicStride + uiPelXPU;  // 画出左边
								PicPel[PelIdx] = 41;
							}
							PelIdx = (uiPelYPU + i) * PicStride + uiRPelXPU;  // 画出右边
							PicPel[PelIdx] = 41;
						}
					}
					else if (comp == 1) // lxy U分量
					{
						UInt uiPelXPU_U = uiPelXPU / 2;
						UInt uiPelYPU_U = uiPelYPU / 2;
						UInt riWidthPU_U = riWidthPU / 2;
						UInt riHeightPU_U = riHeightPU / 2;
						UInt uiRPelXPU_U = uiRPelXPU / 2;
						UInt uiBPelYPU_U = uiBPelYPU / 2;
						for (UInt i = 0; i < riWidthPU_U; i++)
						{
							UInt PelIdx;
							// PelIdx = uiPelYPU_U * PicStride + uiPelXPU_U + i; // lxy 画出上下两条边
							// PicPel[PelIdx] = 240;
							if (uiPelYPU_U == 0)  // 图像上边界
							{
								PelIdx = uiPelYPU_U * PicStride + uiPelXPU_U + i;  // 画出上边
								PicPel[PelIdx] = 240;
							}
							PelIdx = uiBPelYPU_U * PicStride + uiPelXPU_U + i;  // 画出下边
							PicPel[PelIdx] = 240;
						}
						for (UInt i = 0; i < riHeightPU_U; i++)
						{
							UInt PelIdx;
							// PelIdx = (uiPelYPU_U + i) * PicStride + uiPelXPU_U; // lxy 画出左右两条边
							// PicPel[PelIdx] = 240;
							if (uiPelXPU_U == 0)  // 图像左边界
							{
								PelIdx = (uiPelYPU_U + i) * PicStride + uiPelXPU_U;  // 画出左边
								PicPel[PelIdx] = 240;
							}
							PelIdx = (uiPelYPU_U + i) * PicStride + uiRPelXPU_U;  // 画出右边
							PicPel[PelIdx] = 240;
						}
					}
					else // lxy V分量
					{
						UInt uiPelXPU_V = uiPelXPU / 2; // lxy 注意：不要再U分量坐标的基础上改变V分量的坐标
						UInt uiPelYPU_V = uiPelYPU / 2;
						UInt riWidthPU_V = riWidthPU / 2;
						UInt riHeightPU_V = riHeightPU / 2;
						UInt uiRPelXPU_V = uiRPelXPU / 2;
						UInt uiBPelYPU_V = uiBPelYPU / 2;
						for (UInt i = 0; i < riWidthPU_V; i++)
						{
							UInt PelIdx;
							// PelIdx = uiPelYPU_V * PicStride + uiPelXPU_V + i; // lxy 画出上下两条边
							// PicPel[PelIdx] = 110;
							if (uiPelYPU_V == 0)  // 图像上边界
							{
								PelIdx = uiPelYPU_V * PicStride + uiPelXPU_V + i;  // 画出上边
								PicPel[PelIdx] = 110;
							}
							PelIdx = uiBPelYPU_V * PicStride + uiPelXPU_V + i;  // 画出下边
							PicPel[PelIdx] = 110;
						}
						for (UInt i = 0; i < riHeightPU_V; i++)
						{
							UInt PelIdx;
							// PelIdx = (uiPelYPU_V + i) * PicStride + uiPelXPU_V; // lxy 画出左右两条边
							// PicPel[PelIdx] = 110;
							if (uiPelXPU_V == 0)  // 图像左边界
							{
								PelIdx = (uiPelYPU_V + i) * PicStride + uiPelXPU_V;  // 画出左边
								PicPel[PelIdx] = 110;
							}
							PelIdx = (uiPelYPU_V + i) * PicStride + uiRPelXPU_V;  // 画出右边
							PicPel[PelIdx] = 110;
						}
					}
				}
			}
			return; // lxy CU中的PU全部画完

		}

#else
		// lxy 绘制所有的PU（蓝色）
		// lxy uiPicSize存储图像的宽和高，因为CU超出图像范围时，不会继续划分为PU（xcompressCU函数），所以此处加一个判断
		if (uiRPelX >= uiPicSize[0] || uiBPelY >= uiPicSize[1])
		{
			return;
		}

		UChar iNumPart = 0;

		// lxy 判断CU划分后的PU数目
		switch (PUType[0])
		{
		case SIZE_2Nx2N:    iNumPart = 1; break;
		case SIZE_2NxN:     iNumPart = 2; break;
		case SIZE_Nx2N:     iNumPart = 2; break;
		case SIZE_NxN:      iNumPart = 4; break;
		case SIZE_2NxnU:    iNumPart = 2; break;
		case SIZE_2NxnD:    iNumPart = 2; break;
		case SIZE_nLx2N:    iNumPart = 2; break;
		case SIZE_nRx2N:    iNumPart = 2; break;
		default:            assert(0);   break; // lxy 最后一行CTU有一部分在图像外，此时CU不继续划分为PU，所以没有存储PU的PartSize，导致这里断开
		}

		UInt  riWidthPU = 0, riHeightPU = 0; // lxy PU的宽和高
		UInt  uiPelXPU = 0, uiPelYPU = 0, uiRPelXPU = 0, uiBPelYPU = 0; // lxy PU左上角和右下角像素的坐标
		UInt  ruiPartAddr = 0; // lxy PU左上角的4x4块在CU中的Z扫描绝对地址
		for (Int iPartIdx = 0; iPartIdx < iNumPart; iPartIdx++)
		{
			switch (PUType[0])
			{
			case SIZE_2NxN:
				riWidthPU = uiSize_X;      riHeightPU = uiSize_Y >> 1; ruiPartAddr = (iPartIdx == 0) ? 0 : uiNumPartition >> 1;
				uiPelXPU = uiPelX;
				uiPelYPU = (iPartIdx == 0) ? uiPelY : uiPelY + riHeightPU;
				uiRPelXPU = uiRPelX;
				uiBPelYPU = (iPartIdx == 0) ? uiBPelY - riHeightPU : uiBPelY;
				break;
			case SIZE_Nx2N:
				riWidthPU = uiSize_X >> 1; riHeightPU = uiSize_Y;      ruiPartAddr = (iPartIdx == 0) ? 0 : uiNumPartition >> 2;
				uiPelXPU = (iPartIdx == 0) ? uiPelX : uiPelX + riWidthPU;
				uiPelYPU = uiPelY;
				uiRPelXPU = (iPartIdx == 0) ? uiRPelX - riWidthPU : uiRPelX;
				uiBPelYPU = uiBPelY;
				break;
			case SIZE_NxN:
				riWidthPU = uiSize_X >> 1; riHeightPU = uiSize_Y >> 1; ruiPartAddr = (uiNumPartition >> 2) * iPartIdx;
				switch (iPartIdx)
				{
				case 0:
					uiPelXPU = uiPelX;  uiPelYPU = uiPelY;  uiRPelXPU = uiRPelX - riWidthPU;  uiBPelYPU = uiBPelY - riHeightPU;
					break;
				case 1:
					uiPelXPU = uiPelX + riWidthPU;  uiPelYPU = uiPelY;  uiRPelXPU = uiRPelX;  uiBPelYPU = uiBPelY - riHeightPU;
					break;
				case 2:
					uiPelXPU = uiPelX;  uiPelYPU = uiPelY + riHeightPU;  uiRPelXPU = uiRPelX - riWidthPU;  uiBPelYPU = uiBPelY;
					break;
				case 3:
					uiPelXPU = uiPelX + riWidthPU;  uiPelYPU = uiPelY + riHeightPU;  uiRPelXPU = uiRPelX;  uiBPelYPU = uiBPelY;
					break;
				default:
					uiPelXPU = uiPelX;  uiPelYPU = uiPelY;  uiRPelXPU = uiRPelX;  uiBPelYPU = uiBPelY; // lxy 不会过缺省的情况
					break;
				}
				break;
			case SIZE_2NxnU:
				riWidthPU = uiSize_X;
				riHeightPU = (iPartIdx == 0) ? uiSize_Y >> 2 : (uiSize_Y >> 2) + (uiSize_Y >> 1);
				ruiPartAddr = (iPartIdx == 0) ? 0 : uiNumPartition >> 3;
				uiPelXPU = uiPelX;
				uiPelYPU = (iPartIdx == 0) ? uiPelY : uiPelY + (uiSize_Y - riHeightPU);
				uiRPelXPU = uiRPelX;
				uiBPelYPU = (iPartIdx == 0) ? uiBPelY - (uiSize_Y - riHeightPU) : uiBPelY;
				break;
			case SIZE_2NxnD:
				riWidthPU = uiSize_X;
				riHeightPU = (iPartIdx == 0) ? (uiSize_Y >> 2) + (uiSize_Y >> 1) : uiSize_Y >> 2;
				ruiPartAddr = (iPartIdx == 0) ? 0 : (uiNumPartition >> 1) + (uiNumPartition >> 3);
				uiPelXPU = uiPelX;
				uiPelYPU = (iPartIdx == 0) ? uiPelY : uiPelY + (uiSize_Y - riHeightPU);
				uiRPelXPU = uiRPelX;
				uiBPelYPU = (iPartIdx == 0) ? uiBPelY - (uiSize_Y - riHeightPU) : uiBPelY;
				break;
			case SIZE_nLx2N:
				riWidthPU = (iPartIdx == 0) ? uiSize_X >> 2 : (uiSize_X >> 2) + (uiSize_X >> 1);
				riHeightPU = uiSize_Y;
				ruiPartAddr = (iPartIdx == 0) ? 0 : uiNumPartition >> 4;
				uiPelXPU = (iPartIdx == 0) ? uiPelX : uiPelX + (uiSize_X - riWidthPU);
				uiPelYPU = uiPelY;
				uiRPelXPU = (iPartIdx == 0) ? uiRPelX - (uiSize_X - riWidthPU) : uiRPelX;
				uiBPelYPU = uiBPelY;
				break;
			case SIZE_nRx2N:
				riWidthPU = (iPartIdx == 0) ? (uiSize_X >> 2) + (uiSize_X >> 1) : uiSize_X >> 2;
				riHeightPU = uiSize_Y;
				ruiPartAddr = (iPartIdx == 0) ? 0 : (uiNumPartition >> 2) + (uiNumPartition >> 4);
				uiPelXPU = (iPartIdx == 0) ? uiPelX : uiPelX + (uiSize_X - riWidthPU);
				uiPelYPU = uiPelY;
				uiRPelXPU = (iPartIdx == 0) ? uiRPelX - (uiSize_X - riWidthPU) : uiRPelX;
				uiBPelYPU = uiBPelY;
				break;
			default:
				assert(PUType[0] == SIZE_2Nx2N);
				riWidthPU = uiSize_X;      riHeightPU = uiSize_Y;      ruiPartAddr = 0;
				uiPelXPU = uiPelX;        uiPelYPU = uiPelY;        uiRPelXPU = uiRPelX;        uiBPelYPU = uiBPelY;
				break;
			}

			for (UInt comp = 0; comp < pcPicYuvRec->getNumberValidComponents(); comp++) // lxy 画出PU的划分线
			{
				const ComponentID compID = ComponentID(comp);
				const UInt width = pcPicYuvRec->getWidth(compID); // lxy width和height是原始图像的宽和高
				const UInt height = pcPicYuvRec->getHeight(compID);
				Pel *PicPel = pcPicYuvRec->getAddr(compID); // lxy 得到每个颜色分量的值
				const UInt PicStride = pcPicYuvRec->getStride(compID); // lxy stride原始图像的基础上添加了填充部分
				if (comp == 0)
				{
					for (UInt i = 0; i < riWidthPU; i++)
					{
						UInt PelIdx;
						// PelIdx = uiPelYPU * PicStride + uiPelXPU + i; // lxy 画出上下两条边
						// PicPel[PelIdx] = 41;
						if (uiPelYPU == 0)  // 图像上边界（避免划分线重复，除了边界处，只画下边和右边）
						{
							PelIdx = uiPelYPU * PicStride + uiPelXPU + i;  // 画出上边
							PicPel[PelIdx] = 41;
						}
						PelIdx = uiBPelYPU * PicStride + uiPelXPU + i;  // 画出下边
						PicPel[PelIdx] = 41;
					}
					for (UInt i = 0; i < riHeightPU; i++)
					{
						UInt PelIdx;
						// PelIdx = (uiPelYPU + i) * PicStride + uiPelXPU; // lxy 画出左右两条边
						// PicPel[PelIdx] = 41;
						if (uiPelXPU == 0)  // 图像左边界（避免划分线重复，除了边界处，只画下边和右边）
						{
							PelIdx = (uiPelYPU + i) * PicStride + uiPelXPU;  // 画出左边
							PicPel[PelIdx] = 41;
						}
						PelIdx = (uiPelYPU + i) * PicStride + uiRPelXPU;  // 画出右边
						PicPel[PelIdx] = 41;
					}
				}
				else if (comp == 1) // lxy U分量
				{
					UInt uiPelXPU_U = uiPelXPU / 2;
					UInt uiPelYPU_U = uiPelYPU / 2;
					UInt riWidthPU_U = riWidthPU / 2;
					UInt riHeightPU_U = riHeightPU / 2;
					UInt uiRPelXPU_U = uiRPelXPU / 2;
					UInt uiBPelYPU_U = uiBPelYPU / 2;
					for (UInt i = 0; i < riWidthPU_U; i++)
					{
						UInt PelIdx;
						// PelIdx = uiPelYPU_U * PicStride + uiPelXPU_U + i; // lxy 画出上下两条边
						// PicPel[PelIdx] = 240;
						if (uiPelYPU_U == 0)  // 图像上边界
						{
							PelIdx = uiPelYPU_U * PicStride + uiPelXPU_U + i;  // 画出上边
							PicPel[PelIdx] = 240;
						}
						PelIdx = uiBPelYPU_U * PicStride + uiPelXPU_U + i;  // 画出下边
						PicPel[PelIdx] = 240;
					}
					for (UInt i = 0; i < riHeightPU_U; i++)
					{
						UInt PelIdx;
						// PelIdx = (uiPelYPU_U + i) * PicStride + uiPelXPU_U; // lxy 画出左右两条边
						// PicPel[PelIdx] = 240;
						if (uiPelXPU_U == 0)  // 图像左边界
						{
							PelIdx = (uiPelYPU_U + i) * PicStride + uiPelXPU_U;  // 画出左边
							PicPel[PelIdx] = 240;
						}
						PelIdx = (uiPelYPU_U + i) * PicStride + uiRPelXPU_U;  // 画出右边
						PicPel[PelIdx] = 240;
					}
				}
				else // lxy V分量
				{
					UInt uiPelXPU_V = uiPelXPU / 2; // lxy 注意：不要再U分量坐标的基础上改变V分量的坐标
					UInt uiPelYPU_V = uiPelYPU / 2;
					UInt riWidthPU_V = riWidthPU / 2;
					UInt riHeightPU_V = riHeightPU / 2;
					UInt uiRPelXPU_V = uiRPelXPU / 2;
					UInt uiBPelYPU_V = uiBPelYPU / 2;
					for (UInt i = 0; i < riWidthPU_V; i++)
					{
						UInt PelIdx;
						// PelIdx = uiPelYPU_V * PicStride + uiPelXPU_V + i; // lxy 画出上下两条边
						// PicPel[PelIdx] = 110;
						if (uiPelYPU_V == 0)  // 图像上边界
						{
							PelIdx = uiPelYPU_V * PicStride + uiPelXPU_V + i;  // 画出上边
							PicPel[PelIdx] = 110;
						}
						PelIdx = uiBPelYPU_V * PicStride + uiPelXPU_V + i;  // 画出下边
						PicPel[PelIdx] = 110;
					}
					for (UInt i = 0; i < riHeightPU_V; i++)
					{
						UInt PelIdx;
						// PelIdx = (uiPelYPU_V + i) * PicStride + uiPelXPU_V; // lxy 画出左右两条边
						// PicPel[PelIdx] = 110;
						if (uiPelXPU_V == 0)  // 图像左边界
						{
							PelIdx = (uiPelYPU_V + i) * PicStride + uiPelXPU_V;  // 画出左边
							PicPel[PelIdx] = 110;
						}
						PelIdx = (uiPelYPU_V + i) * PicStride + uiRPelXPU_V;  // 画出右边
						PicPel[PelIdx] = 110;
					}
				}
			}
		}
		return; // lxy CU中的PU全部画完 
#endif



		/*
		// lxy 绘制CU划分线（红色）
		// lxy uiPicSize存储图像的宽和高，当CU超出图像范围时，不再画CU的划分线（xcompressCU函数），所以此处加一个判断
		if (uiRPelX >= uiPicSize[0] || uiBPelY >= uiPicSize[1])
		{
			return;
		}

		for (UInt comp = 0; comp < pcPicYuvRec->getNumberValidComponents(); comp++)
		{
			const ComponentID compID = ComponentID(comp);
			const UInt width = pcPicYuvRec->getWidth(compID); // lxy width和height是原始图像的宽和高
			const UInt height = pcPicYuvRec->getHeight(compID);
			Pel *PicPel = pcPicYuvRec->getAddr(compID); // lxy 得到每个颜色分量的值
			const UInt PicStride = pcPicYuvRec->getStride(compID); // lxy stride原始图像的基础上添加了填充部分
			if (comp == 0)
			{
				for (UInt i = 0; i < uiSize_X; i++)
				{
					UInt PelIdx;
					// PelIdx = uiPelY * PicStride + uiPelX + i; // lxy 画出上下两条边
					// PicPel[PelIdx] = 82;
					if (uiPelY == 0) // 图像上边界（避免划分线重复，除了边界处，只画下边和右边）
					{
						PelIdx = uiPelY * PicStride + uiPelX + i;  // 画出上边
						PicPel[PelIdx] = 82;
					}
					PelIdx = uiBPelY * PicStride + uiPelX + i;  // 画出下边
					PicPel[PelIdx] = 82;
				}
				for (UInt i = 0; i < uiSize_Y; i++)
				{
					UInt PelIdx;
					// UInt PelIdx = (uiPelY + i) * PicStride + uiPelX; // lxy 画出左右两条边
					// PicPel[PelIdx] = 82;
					if (uiPelX == 0)  // 图像左边界（避免划分线重复，除了边界处，只画下边和右边）
					{
						PelIdx = (uiPelY + i) * PicStride + uiPelX;  // 画出左边
						PicPel[PelIdx] = 82;
					}
					PelIdx = (uiPelY + i) * PicStride + uiRPelX;  // 画出右边
					PicPel[PelIdx] = 82;
				}
			}
			else if (comp == 1) // lxy U分量
			{
				UInt uiPelX_U = uiPelX / 2;
				UInt uiPelY_U = uiPelY / 2;
				UInt uiSize_X_U = uiSize_X / 2;
				UInt uiSize_Y_U = uiSize_Y / 2;
				UInt uiRPelX_U = uiRPelX / 2;
				UInt uiBPelY_U = uiBPelY / 2;
				for (UInt i = 0; i < uiSize_X_U; i++)
				{
					UInt PelIdx;
					// PelIdx = uiPelY_U * PicStride + uiPelX_U + i; // lxy 画出上下两条边
					// PicPel[PelIdx] = 90;
					if (uiPelY_U == 0)  // 图像上边界
					{
						PelIdx = uiPelY_U * PicStride + uiPelX_U + i;  // 画出上边
						PicPel[PelIdx] = 90;
					}
					PelIdx = uiBPelY_U * PicStride + uiPelX_U + i;  // 画出下边
					PicPel[PelIdx] = 90;
				}
				for (UInt i = 0; i < uiSize_Y_U; i++)
				{
					UInt PelIdx;
					// PelIdx = (uiPelY_U + i) * PicStride + uiPelX_U; // lxy 画出左右两条边
					// PicPel[PelIdx] = 90;
					if (uiPelX_U == 0)  // 图像左边界
					{
						PelIdx = (uiPelY_U + i) * PicStride + uiPelX_U;  // 画出左边
						PicPel[PelIdx] = 90;
					}
					PelIdx = (uiPelY_U + i) * PicStride + uiRPelX_U;  // 画出右边
					PicPel[PelIdx] = 90;
				}
			}
			else // lxy V分量
			{
				UInt uiPelX_V = uiPelX / 2; // lxy 注意：不要再U分量坐标的基础上改变V分量的坐标
				UInt uiPelY_V = uiPelY / 2;
				UInt uiSize_X_V = uiSize_X / 2;
				UInt uiSize_Y_V = uiSize_Y / 2;
				UInt uiRPelX_V = uiRPelX / 2;
				UInt uiBPelY_V = uiBPelY / 2;
				for (UInt i = 0; i < uiSize_X_V; i++)
				{
					UInt PelIdx;
					// PelIdx = uiPelY_V * PicStride + uiPelX_V + i; // lxy 画出上下两条边
					// PicPel[PelIdx] = 240;
					if (uiPelY_V == 0)  // 图像上边界
					{
						PelIdx = uiPelY_V * PicStride + uiPelX_V + i;  // 画出上边
						PicPel[PelIdx] = 240;
					}
					PelIdx = uiBPelY_V * PicStride + uiPelX_V + i;  // 画出下边
					PicPel[PelIdx] = 240;
				}
				for (UInt i = 0; i < uiSize_Y_V; i++)
				{
					UInt PelIdx;
					// PelIdx = (uiPelY_V + i) * PicStride + uiPelX_V; // lxy 画出左右两条边
					// PicPel[PelIdx] = 240;
					if (uiPelX_V == 0)  // 图像左边界
					{
						PelIdx = (uiPelY_V + i) * PicStride + uiPelX_V;  // 画出左边
						PicPel[PelIdx] = 240;
					}
					PelIdx = (uiPelY_V + i) * PicStride + uiRPelX_V;  // 画出右边
					PicPel[PelIdx] = 240;
				}
			}
		}
		return;  // lxy 当前深度的CU画完 */


	}
	else
	{
		// lxy 划分深度小于4，further drawing
		UInt uiNextSize_X = uiSize_X / 2;
		UInt uiNextSize_Y = uiSize_Y / 2;
		UChar uiDepthNext = (UChar)(UInt(uiDepth) + 1);
		UInt uiNumPartitionNext = uiNumPartition / 4;
		if (uiDepthNext < 4)
		{
			for (UInt iAbsPartY = 0; iAbsPartY < 2; iAbsPartY++) // lxy 四叉树划分
			{
				for (UInt iAbsPartX = 0; iAbsPartX < 2; iAbsPartX++)
				{

					UInt uiNextPelX = 0, uiNextPelY = 0;
					UInt  iAbsPartIndex = iAbsPartY * 2 + iAbsPartX;
					UChar* iNextDepth = new UChar[uiNumPartitionNext];
					SChar* NextPUType = new SChar[uiNumPartitionNext];
					SChar* NextPUPredMode = new SChar[uiNumPartitionNext];
					UInt*  uiNextRefLayerId = new UInt[uiNumPartitionNext];
					for (int i = 0; i < uiNumPartitionNext; i++)
					{
						iNextDepth[i] = DepthCTU[i + iAbsPartIndex * uiNumPartitionNext]; // lxy 赋值CU划分深度、PU划分类型、预测模式、参考图像所在层的索引号信息
						NextPUType[i] = PUType[i + iAbsPartIndex * uiNumPartitionNext];
						NextPUPredMode[i] = PUPredMode[i + iAbsPartIndex * uiNumPartitionNext];
						uiNextRefLayerId[i] = uiRefLayerId[i + iAbsPartIndex * uiNumPartitionNext];
					}
					uiNextPelX = uiPelX + iAbsPartX * uiNextSize_X;
					uiNextPelY = uiPelY + iAbsPartY * uiNextSize_Y;

#if Debug_PartitionAccuracy
					// uiabsZIdxInCtu  当前CU左上角的4x4单元在CTU中的绝对Z扫描索引
					PUPartInPic(pcPicYuvRec, pcCtu, iNextDepth, NextPUType, ContrastCUDepth, ContrastPUType, NextPUPredMode, uiNextRefLayerId, uiNextPelX, uiNextPelY, uiNextSize_X, uiNextSize_Y, uiPicSize, uiDepthNext, uiabsZIdxInCtu);
					uiabsZIdxInCtu += uiNumPartitionNext;
#else
					PUPartInPic(pcPicYuvRec, iNextDepth, NextPUType, NextPUPredMode, uiNextRefLayerId, uiNextPelX, uiNextPelY, uiNextSize_X, uiNextSize_Y, uiPicSize, uiDepthNext);
#endif

					delete[] iNextDepth; // lxy 释放内存
					delete[] NextPUType;
					delete[] uiNextRefLayerId;

				}

			}
		}
	}
}
#endif

Void TAppEncTop::xWriteRecon(UInt layer, Int iNumEncoded)
{
	ChromaFormat& chromaFormatIdc = m_apcLayerCfg[layer]->m_chromaFormatIDC;
	Int xScal = TComSPS::getWinUnitX(chromaFormatIdc);
	Int yScal = TComSPS::getWinUnitY(chromaFormatIdc);

	const InputColourSpaceConversion ipCSC = (!m_outputInternalColourSpace) ? m_inputColourSpaceConvert : IPCOLOURSPACE_UNCHANGED;

	if (m_isField)
	{
		//Reinterlace fields
		Int i;
		TComList<TComPicYuv*>::iterator iterPicYuvRec = m_acListPicYuvRec[layer].end();

		for (i = 0; i < iNumEncoded; i++)
		{
			--iterPicYuvRec;
		}

		for (i = 0; i < iNumEncoded / 2; i++)
		{
			TComPicYuv*  pcPicYuvRecTop = *(iterPicYuvRec++);
			TComPicYuv*  pcPicYuvRecBottom = *(iterPicYuvRec++);

			if (!m_apcLayerCfg[layer]->getReconFileName().empty() && pcPicYuvRecTop->isReconstructed() && pcPicYuvRecBottom->isReconstructed())
			{
				m_apcTVideoIOYuvReconFile[layer]->write(pcPicYuvRecTop, pcPicYuvRecBottom, ipCSC, m_apcLayerCfg[layer]->getConfWinLeft() * xScal, m_apcLayerCfg[layer]->getConfWinRight() * xScal,
					m_apcLayerCfg[layer]->getConfWinTop() * yScal, m_apcLayerCfg[layer]->getConfWinBottom() * yScal, NUM_CHROMA_FORMAT, m_isTopFieldFirst);
			}
		}
	}
	else
	{
		Int i;

		TComList<TComPicYuv*>::iterator iterPicYuvRec = m_acListPicYuvRec[layer].end();

#if Debug_PartitionInformation
		TComList<TComPic*> pcListPic = *(m_apcTEncTop[layer]->getListPic()); // lxy 提取当前层的DPB图像列表
		TComList<TComPic*>::iterator iterpcListPic = pcListPic.end(); // lxy end()函数是列表中最后一个元素的结束地址+1，不是最后一个元素的首地址
		TComPic* pcPic = *(--iterpcListPic); // lxy 先减1获取所需参数，后面再加1变为原来的地址
		UInt uiCTURow = pcPic->getFrameHeightInCtus();
		UInt uiCTUCol = pcPic->getFrameWidthInCtus();
		UInt uiPicSize[2] = { pcPic->getPicSym()->getSPS().getPicWidthInLumaSamples(), pcPic->getPicSym()->getSPS().getPicHeightInLumaSamples() };
		TComList<TComDataCU*>* pcListCTU = m_apcTEncTop[layer]->getListCTU(); // lxy 提取编码CTU列表的指针
		Int iSize = Int(pcListCTU->size());
		TComList<TComDataCU*>::iterator iterpcListCTU = pcListCTU->begin();  // lxy 获取当前GOP的第一个CTU

		/*
		for (i = 0; i < (iSize - iNumEncoded * uiCTURow*uiCTUCol); i++)
		{
			pcListCTU.erase(iterpcListCTURelease); // lxy 删除列表中划分的CTU（图像以GOP为单位输出，CTU以GOP为单位划分）的存储地址，剩余CTU的存储地址不变，只有索引变化
			iterpcListCTURelease = pcListCTU.begin();
		}


		TComList<TComDataCU*>::iterator iterpcListCTU = iterpcListCTURelease; // lxy 获取当前GOP的第一个CTU
		*/

		/*
		TComList<TComDataCU*>::iterator iterpcListCTU = pcListCTU.end();

		for (Int i = 0; i < iNumEncoded; i++)
		{
			for (UInt row = 0; row < uiCTURow; row++)
			{
				for (UInt col = 0; col < uiCTUCol; col++)
				{
					--iterpcListCTU;
				}
			}
		}*/

		iterpcListPic++; // lxy 加1变为原来的地址

#endif

		for (i = 0; i < iNumEncoded; i++)
		{
			--iterPicYuvRec;
		}

		for (i = 0; i < iNumEncoded; i++)
		{
			TComPicYuv*  pcPicYuvRec = *(iterPicYuvRec++);

#if Debug_PartitionInformation
			// lxy 绘制PU划分线
			UInt uiLPelX = 0, uiTPelY = 0; // lxy 定义CU左上角像素的坐标
			UInt uiCUWidth = 0, uiCUHeight = 0; // lxy 定义CU的宽和高
			UChar* uiDepth = new UChar[256]; // lxy 定义 4x4 块的深度
			SChar* PUType = new SChar[256]; // lxy 定义 4x4 块的PU类型
			SChar* PUPredMode = new SChar[256]; // lxy 定义 4x4 块的预测模式
			UInt*  uiRefLayerId = new UInt[256]; // lxy 定义 4x4 块的参考层号
			Int    iRefIdx = 0; // lxy 定义参考索引

#if Debug_PartitionAccuracy
	  // 读入存储CU划分深度和PU预测模式的txt文件
			fstream fp_CU("data_cu_Cactus.txt", ios::binary | ios::in);  // 读入CU划分深度文件
			fstream fp_PU("data_pu_Cactus.txt", ios::binary | ios::in);  // 读入PU预测模式文件
			std::string buffer_CU, buffer_PU;
			//Int RowNum = 510, ColNum = 256;
			Int RowNum = 1000, ColNum = 256;
			Int **CUDepthArray = new Int *[RowNum];// CU划分深度
			Int **PUModeArray = new Int *[RowNum];// PU预测模式
			for (Int i = 0; i < RowNum; i++)
			{
				CUDepthArray[i] = new Int[ColNum];// 每一行的CU划分深度
				PUModeArray[i] = new Int[ColNum];// 每一行的PU预测模式
				std::memset(CUDepthArray[i], 0, sizeof(Int)*ColNum);
				std::memset(PUModeArray[i], 0, sizeof(Int)*ColNum);

			}

			// 按行读入CU划分深度文件
			Int RowsIdx = 0;
			while (getline(fp_CU, buffer_CU))
			{
				stringstream word(buffer_CU);  // lxy 将每一行数据按空格分开并存储
				for (Int j = 0; j < ColNum; j++)
				{
					word >> CUDepthArray[RowsIdx][j];
				}
				RowsIdx++;
			}

			// 按行读入PU预测模式文件
			RowsIdx = 0;
			while (getline(fp_PU, buffer_PU))
			{
				stringstream word(buffer_PU);  // lxy 将每一行数据按空格分开并存储
				for (Int j = 0; j < ColNum; j++)
				{
					word >> PUModeArray[RowsIdx][j];
				}
				RowsIdx++;
			}
#endif

			//UInt uiCTURow = pcPic->getFrameHeightInCtus();
			//UInt uiCTUCol = pcPic->getFrameWidthInCtus();
			for (UInt ctuR = 0; ctuR < uiCTURow; ctuR++)
			{
				for (UInt ctuC = 0; ctuC < uiCTUCol; ctuC++)
				{
					//UInt ctuRsAddr = ctuR * uiCTUCol + ctuC;
					//TComDataCU* pCtu = pcPic->getCtu(ctuRsAddr);
					TComDataCU* pCtu = *(iterpcListCTU++);
					uiLPelX = pCtu->getCUPelX(); // lxy 获取CTU左上角像素的坐标
					uiTPelY = pCtu->getCUPelY();
					uiCUWidth = pCtu->getWidth(0);
					uiCUHeight = pCtu->getHeight(0);
					for (UInt uiAbsPartAddr = 0; uiAbsPartAddr < 256; uiAbsPartAddr++)
					{
						// CTU内4x4 CU的Depth、PartitionSize、PredictionMode、RefIdx信息按Z扫描的顺序存储
						uiDepth[uiAbsPartAddr] = pCtu->getDepth(uiAbsPartAddr); // lxy 获取划分深度信息
						PUType[uiAbsPartAddr] = pCtu->getPartitionSize(uiAbsPartAddr); // lxy 获取PU划分类型信息
						PUPredMode[uiAbsPartAddr] = pCtu->getPredictionMode(uiAbsPartAddr); // lxy 获取预测模式信息
						if (pCtu->getPredictionMode(uiAbsPartAddr) == MODE_INTER)
						{
							Int  iNumPredDir = pCtu->getSlice()->isInterP() ? 1 : 2; // lxy 确定预测方向的数目
							for (Int iRefList = 0; iRefList < iNumPredDir; iRefList++)
							{
								RefPicList  eRefPicList = (iRefList ? REF_PIC_LIST_1 : REF_PIC_LIST_0);
								iRefIdx = pCtu->getCUMvField(eRefPicList)->getRefIdx(uiAbsPartAddr);
								uiRefLayerId[uiAbsPartAddr] = pCtu->getSlice()->getRefPic(eRefPicList, iRefIdx)->getLayerId(); // lxy 获取参考图像所在层的索引号信息
							}
						}
						else // 只有帧间预测模式才有参考图像
						{
							uiRefLayerId[uiAbsPartAddr] = 0;
						}

					}

#if Debug_PartitionAccuracy
					UInt ctuRsAddr = ctuR * uiCTUCol + ctuC;
					UInt uiabsZIdxInCurCtu = 0;  // 当前CTU中的绝对Z扫描索引
					PUPartInPic(pcPicYuvRec, pCtu, uiDepth, PUType, CUDepthArray[ctuRsAddr], PUModeArray[ctuRsAddr], PUPredMode, uiRefLayerId, uiLPelX, uiTPelY, uiCUWidth, uiCUHeight, uiPicSize, 0, uiabsZIdxInCurCtu); // lxy 输入CTU的信息
#else
					PUPartInPic(pcPicYuvRec, uiDepth, PUType, PUPredMode, uiRefLayerId, uiLPelX, uiTPelY, uiCUWidth, uiCUHeight, uiPicSize, 0); // lxy 输入CTU的信息
#endif


				}
			}

			delete[] uiDepth; // lxy 释放内存
			delete[] PUType;
			delete[] uiRefLayerId;

#if Debug_PartitionAccuracy
			// lxy 释放内存
			for (Int i = 0; i < RowNum; i++)
			{
				delete[] CUDepthArray[i], PUModeArray[i];
			}

			delete[] CUDepthArray, PUModeArray;
#endif

#endif

			if (!m_apcLayerCfg[layer]->getReconFileName().empty() && pcPicYuvRec->isReconstructed())
			{
				m_apcTVideoIOYuvReconFile[layer]->write(pcPicYuvRec, ipCSC, m_apcLayerCfg[layer]->getConfWinLeft() * xScal, m_apcLayerCfg[layer]->getConfWinRight() * xScal,
					m_apcLayerCfg[layer]->getConfWinTop() * yScal, m_apcLayerCfg[layer]->getConfWinBottom() * yScal,
					NUM_CHROMA_FORMAT, m_bClipOutputVideoToRec709Range);
			}
		}

#if Debug_PartitionInformation
		TComList<TComDataCU*>::iterator iterpcListCTURelease = pcListCTU->begin();  // lxy 获取当前GOP的第一个CTU
		for (Int i = 0; i < pcListCTU->size(); i++)
		{
			delete *(iterpcListCTURelease++);  // GOP划分完成后，清空存储的CTU，避免内存泄漏（先删除列表中的元素，然后列表索引+1）
			// iterpcListCTURelease++;
		}
		pcListCTU->clear();  // 清空CTU列表
#endif

	}
}

Void TAppEncTop::xWriteStream(std::ostream& bitstreamFile, Int iNumEncoded, const std::list<AccessUnit>& accessUnits)
{
	if (m_isField)
	{
		//Reinterlace fields
		Int i;
		list<AccessUnit>::const_iterator iterBitstream = accessUnits.begin();

		for (i = 0; i < iNumEncoded / 2 && iterBitstream != accessUnits.end(); i++)
		{
			const AccessUnit& auTop = *(iterBitstream++);
			const vector<UInt>& statsTop = writeAnnexB(bitstreamFile, auTop);
			rateStatsAccum(auTop, statsTop);

			const AccessUnit& auBottom = *(iterBitstream++);
			const vector<UInt>& statsBottom = writeAnnexB(bitstreamFile, auBottom);
			rateStatsAccum(auBottom, statsBottom);
		}
	}
	else
	{
		Int i;

		list<AccessUnit>::const_iterator iterBitstream = accessUnits.begin();

		for (i = 0; i < iNumEncoded && iterBitstream != accessUnits.end(); i++)
		{
			const AccessUnit& au = *(iterBitstream++);
			const vector<UInt>& stats = writeAnnexB(bitstreamFile, au);
			rateStatsAccum(au, stats);
		}
	}
}

#else // SVC_EXTENSION
Void TAppEncTop::xGetBuffer(TComPicYuv*& rpcPicYuvRec)
{
	assert(m_iGOPSize > 0);

	// org. buffer
	if (m_cListPicYuvRec.size() >= (UInt)m_iGOPSize) // buffer will be 1 element longer when using field coding, to maintain first field whilst processing second.
	{
		rpcPicYuvRec = m_cListPicYuvRec.popFront();

	}
	else
	{
		rpcPicYuvRec = new TComPicYuv;

		rpcPicYuvRec->create(m_iSourceWidth, m_iSourceHeight, m_chromaFormatIDC, m_uiMaxCUWidth, m_uiMaxCUHeight, m_uiMaxTotalCUDepth, true);

	}
	m_cListPicYuvRec.pushBack(rpcPicYuvRec);
}

Void TAppEncTop::xDeleteBuffer()
{
	TComList<TComPicYuv*>::iterator iterPicYuvRec = m_cListPicYuvRec.begin();

	Int iSize = Int(m_cListPicYuvRec.size());

	for (Int i = 0; i < iSize; i++)
	{
		TComPicYuv*  pcPicYuvRec = *(iterPicYuvRec++);
		pcPicYuvRec->destroy();
		delete pcPicYuvRec; pcPicYuvRec = NULL;
	}

}

/**
  Write access units to output file.
  \param bitstreamFile  target bitstream file
  \param iNumEncoded    number of encoded frames
  \param accessUnits    list of access units to be written
 */
Void TAppEncTop::xWriteOutput(std::ostream& bitstreamFile, Int iNumEncoded, const std::list<AccessUnit>& accessUnits)
{
	const InputColourSpaceConversion ipCSC = (!m_outputInternalColourSpace) ? m_inputColourSpaceConvert : IPCOLOURSPACE_UNCHANGED;

	if (m_isField)
	{
		//Reinterlace fields
		Int i;
		TComList<TComPicYuv*>::iterator iterPicYuvRec = m_cListPicYuvRec.end();
		list<AccessUnit>::const_iterator iterBitstream = accessUnits.begin();

		for (i = 0; i < iNumEncoded; i++)
		{
			--iterPicYuvRec;
		}

		for (i = 0; i < iNumEncoded / 2; i++)
		{
			TComPicYuv*  pcPicYuvRecTop = *(iterPicYuvRec++);
			TComPicYuv*  pcPicYuvRecBottom = *(iterPicYuvRec++);

			if (!m_reconFileName.empty())
			{
				m_cTVideoIOYuvReconFile.write(pcPicYuvRecTop, pcPicYuvRecBottom, ipCSC, m_confWinLeft, m_confWinRight, m_confWinTop, m_confWinBottom, NUM_CHROMA_FORMAT, m_isTopFieldFirst);
			}

			const AccessUnit& auTop = *(iterBitstream++);
			const vector<UInt>& statsTop = writeAnnexB(bitstreamFile, auTop);
			rateStatsAccum(auTop, statsTop);

			const AccessUnit& auBottom = *(iterBitstream++);
			const vector<UInt>& statsBottom = writeAnnexB(bitstreamFile, auBottom);
			rateStatsAccum(auBottom, statsBottom);
		}
	}
	else
	{
		Int i;

		TComList<TComPicYuv*>::iterator iterPicYuvRec = m_cListPicYuvRec.end();
		list<AccessUnit>::const_iterator iterBitstream = accessUnits.begin();

		for (i = 0; i < iNumEncoded; i++)
		{
			--iterPicYuvRec;
		}

		for (i = 0; i < iNumEncoded; i++)
		{
			TComPicYuv*  pcPicYuvRec = *(iterPicYuvRec++);
			if (!m_reconFileName.empty())
			{
				m_cTVideoIOYuvReconFile.write(pcPicYuvRec, ipCSC, m_confWinLeft, m_confWinRight, m_confWinTop, m_confWinBottom,
					NUM_CHROMA_FORMAT, m_bClipOutputVideoToRec709Range);
			}

			const AccessUnit& au = *(iterBitstream++);
			const vector<UInt>& stats = writeAnnexB(bitstreamFile, au);
			rateStatsAccum(au, stats);
		}
	}
}
#endif

/**
 *
 */
Void TAppEncTop::rateStatsAccum(const AccessUnit& au, const std::vector<UInt>& annexBsizes)
{
	AccessUnit::const_iterator it_au = au.begin();
	vector<UInt>::const_iterator it_stats = annexBsizes.begin();

	for (; it_au != au.end(); it_au++, it_stats++)
	{
		switch ((*it_au)->m_nalUnitType)
		{
		case NAL_UNIT_CODED_SLICE_TRAIL_R:
		case NAL_UNIT_CODED_SLICE_TRAIL_N:
		case NAL_UNIT_CODED_SLICE_TSA_R:
		case NAL_UNIT_CODED_SLICE_TSA_N:
		case NAL_UNIT_CODED_SLICE_STSA_R:
		case NAL_UNIT_CODED_SLICE_STSA_N:
		case NAL_UNIT_CODED_SLICE_BLA_W_LP:
		case NAL_UNIT_CODED_SLICE_BLA_W_RADL:
		case NAL_UNIT_CODED_SLICE_BLA_N_LP:
		case NAL_UNIT_CODED_SLICE_IDR_W_RADL:
		case NAL_UNIT_CODED_SLICE_IDR_N_LP:
		case NAL_UNIT_CODED_SLICE_CRA:
		case NAL_UNIT_CODED_SLICE_RADL_N:
		case NAL_UNIT_CODED_SLICE_RADL_R:
		case NAL_UNIT_CODED_SLICE_RASL_N:
		case NAL_UNIT_CODED_SLICE_RASL_R:
		case NAL_UNIT_VPS:
		case NAL_UNIT_SPS:
		case NAL_UNIT_PPS:
			m_essentialBytes += *it_stats;
			break;
		default:
			break;
		}

		m_totalBytes += *it_stats;
	}
}

Void TAppEncTop::printRateSummary()
{
#if SVC_EXTENSION
	Double time = (Double)m_iFrameRcvd / m_apcLayerCfg[m_numLayers - 1]->m_iFrameRate * m_temporalSubsampleRatio;
#else
	Double time = (Double)m_iFrameRcvd / m_iFrameRate * m_temporalSubsampleRatio;
#endif
	printf("Bytes written to file: %u (%.3f kbps)\n", m_totalBytes, 0.008 * m_totalBytes / time);
	if (m_summaryVerboseness > 0)
	{
		printf("Bytes for SPS/PPS/Slice (Incl. Annex B): %u (%.3f kbps)\n", m_essentialBytes, 0.008 * m_essentialBytes / time);
	}
}

#if !SVC_EXTENSION
Void TAppEncTop::printChromaFormat()
{
	std::cout << std::setw(43) << "Input ChromaFormatIDC = ";
	switch (m_InputChromaFormatIDC)
	{
	case CHROMA_400:  std::cout << "  4:0:0"; break;
	case CHROMA_420:  std::cout << "  4:2:0"; break;
	case CHROMA_422:  std::cout << "  4:2:2"; break;
	case CHROMA_444:  std::cout << "  4:4:4"; break;
	default:
		std::cerr << "Invalid";
		exit(1);
	}
	std::cout << std::endl;

	std::cout << std::setw(43) << "Output (internal) ChromaFormatIDC = ";
	switch (m_cTEncTop.getChromaFormatIdc())
	{
	case CHROMA_400:  std::cout << "  4:0:0"; break;
	case CHROMA_420:  std::cout << "  4:2:0"; break;
	case CHROMA_422:  std::cout << "  4:2:2"; break;
	case CHROMA_444:  std::cout << "  4:4:4"; break;
	default:
		std::cerr << "Invalid";
		exit(1);
	}
	std::cout << "\n" << std::endl;
}
#endif

//! \}
