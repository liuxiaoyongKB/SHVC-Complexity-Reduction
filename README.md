Programs for our support vector machine (SVM) based complexity reduction approach for SHVC, in SNR scalability at inter-mode.

# SVM based SHM Encoder
This encoder is used to evaluate the performance of our support vector machine (SVM) based complexity reduction approach 
in SNR scalability at Low-delay P configration. The main part is modified from the standard reference software SHM 12.4 [1], 
coded with C++. The LIBSVM [2], an open source liabrary of SVM is utilized to design CU and PU classifiers as in [3]. 
When performing the quadtree partition of the CTU, the CU classifier is used to determine whether the current CU need to 
continue to be split before selecting the PU mode if the maximum depth is not reached. Thereby unnecessary quad-leaf 
nodes are skipped with the help of CU classifier. Then the PU classifier is utilized to directly predict the candidate set of 
optimal PU modes for the current CU to reduce the number of PU modes to be tested. In this way, most redundant checking of 
RD cost checking can be skipped, thus save the encoding time of enhancement layer (EL) significantly.

# References
[1] JCT-VC, “SHM Software,” [Online]. Available: https://hevc.hhi.fraunhofer.de/svn/svn_SHVCSoftware/tags/SHM-12.4/.

[2] Chang C C, Lin C J. LIBSVM: a library for support vector machines[J]. ACM transactions on intelligent systems and technology (TIST), 2011, 2(3): 1-27.

[3] Zhu L, Zhang Y, Kwong S, et al. Fuzzy SVM-based coding unit decision in HEVC[J]. IEEE Transactions on Broadcasting, 2017, 64(3): 681-694.
