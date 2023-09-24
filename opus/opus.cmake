


if(TARGET_ARM64)
	set(CELT_ARCH_SPECIFIC_FILES
		${OPUS_ROOT}/celt/arm/armcpu.c
		${OPUS_ROOT}/celt/arm/armcpu.h
		#${OPUS_ROOT}/celt/arm/armopts.s.in
		${OPUS_ROOT}/celt/arm/arm_celt_map.c
		#${OPUS_ROOT}/celt/arm/celt_fft_ne10.c
		#${OPUS_ROOT}/celt/arm/celt_mdct_ne10.c
		${OPUS_ROOT}/celt/arm/celt_neon_intr.c
#		${OPUS_ROOT}/celt/arm/celt_pitch_xcorr_arm-gnu.S
	#	${OPUS_ROOT}/celt/arm/celt_pitch_xcorr_arm.s
		${OPUS_ROOT}/celt/arm/fft_arm.h
		${OPUS_ROOT}/celt/arm/fixed_arm64.h
		${OPUS_ROOT}/celt/arm/fixed_armv4.h
		${OPUS_ROOT}/celt/arm/fixed_armv5e.h
		${OPUS_ROOT}/celt/arm/kiss_fft_armv4.h
		${OPUS_ROOT}/celt/arm/kiss_fft_armv5e.h
		${OPUS_ROOT}/celt/arm/mdct_arm.h
		${OPUS_ROOT}/celt/arm/pitch_arm.h
		${OPUS_ROOT}/celt/arm/pitch_neon_intr.c
	)
else()
	set(CELT_ARCH_SPECIFIC_FILES
		${OPUS_ROOT}/celt/x86/celt_lpc_sse.h
		${OPUS_ROOT}/celt/x86/celt_lpc_sse4_1.c
		${OPUS_ROOT}/celt/x86/pitch_sse.c
		${OPUS_ROOT}/celt/x86/pitch_sse.h
		${OPUS_ROOT}/celt/x86/pitch_sse2.c
		${OPUS_ROOT}/celt/x86/pitch_sse4_1.c
		${OPUS_ROOT}/celt/x86/vq_sse.h
		${OPUS_ROOT}/celt/x86/vq_sse2.c
		${OPUS_ROOT}/celt/x86/x86cpu.c
		${OPUS_ROOT}/celt/x86/x86cpu.h
		${OPUS_ROOT}/celt/x86/x86_celt_map.c
	)
endif()

if(TARGET_ARM64)
	set(SILK_ARCH_SPECIFIC_FILES
	#	${OPUS_ROOT}/silk/arm/arm_silk_map.c
		${OPUS_ROOT}/silk/arm/biquad_alt_arm.h
		${OPUS_ROOT}/silk/arm/biquad_alt_neon_intr.c
		${OPUS_ROOT}/silk/arm/LPC_inv_pred_gain_arm.h
		${OPUS_ROOT}/silk/arm/LPC_inv_pred_gain_neon_intr.c
		${OPUS_ROOT}/silk/arm/macros_arm64.h
		${OPUS_ROOT}/silk/arm/macros_armv4.h
		${OPUS_ROOT}/silk/arm/macros_armv5e.h
		${OPUS_ROOT}/silk/arm/NSQ_del_dec_arm.h
		${OPUS_ROOT}/silk/arm/NSQ_del_dec_neon_intr.c
		${OPUS_ROOT}/silk/arm/NSQ_neon.c
		${OPUS_ROOT}/silk/arm/NSQ_neon.h
		${OPUS_ROOT}/silk/arm/SigProc_FIX_armv4.h
		${OPUS_ROOT}/silk/arm/SigProc_FIX_armv5e.h
	)
else()
	set(SILK_ARCH_SPECIFIC_FILES
		${OPUS_ROOT}/silk/x86/main_sse.h
		${OPUS_ROOT}/silk/x86/NSQ_del_dec_sse4_1.c
		${OPUS_ROOT}/silk/x86/NSQ_sse4_1.c
		${OPUS_ROOT}/silk/x86/SigProc_FIX_sse.h
		${OPUS_ROOT}/silk/x86/VAD_sse4_1.c
		${OPUS_ROOT}/silk/x86/VQ_WMat_EC_sse4_1.c
		${OPUS_ROOT}/silk/x86/x86_silk_map.c
	)
endif()


set(OPUS_SRC_FILES
${OPUS_ROOT}/src/analysis.c
${OPUS_ROOT}/src/analysis.h
${OPUS_ROOT}/src/mapping_matrix.c
${OPUS_ROOT}/src/mapping_matrix.h
${OPUS_ROOT}/src/mlp.c
${OPUS_ROOT}/src/mlp.h
${OPUS_ROOT}/src/mlp_data.c
${OPUS_ROOT}/src/opus.c
#${OPUS_ROOT}/src/opus_compare.c
${OPUS_ROOT}/src/opus_decoder.c
#${OPUS_ROOT}/src/opus_demo.c
${OPUS_ROOT}/src/opus_encoder.c
${OPUS_ROOT}/src/opus_multistream.c
${OPUS_ROOT}/src/opus_multistream_decoder.c
${OPUS_ROOT}/src/opus_multistream_encoder.c
${OPUS_ROOT}/src/opus_private.h
${OPUS_ROOT}/src/opus_projection_decoder.c
${OPUS_ROOT}/src/opus_projection_encoder.c
${OPUS_ROOT}/src/repacketizer.c
#${OPUS_ROOT}/src/repacketizer_demo.c
${OPUS_ROOT}/src/tansig_table.h

${OPUS_ROOT}/include/opus.h
${OPUS_ROOT}/include/opus_custom.h
${OPUS_ROOT}/include/opus_defines.h
${OPUS_ROOT}/include/opus_multistream.h
${OPUS_ROOT}/include/opus_projection.h
${OPUS_ROOT}/include/opus_types.h


${OPUS_ROOT}/celt/arch.h
${OPUS_ROOT}/celt/bands.c
${OPUS_ROOT}/celt/bands.h
${OPUS_ROOT}/celt/celt.c
${OPUS_ROOT}/celt/celt.h
${OPUS_ROOT}/celt/celt_decoder.c
${OPUS_ROOT}/celt/celt_encoder.c
${OPUS_ROOT}/celt/celt_lpc.c
${OPUS_ROOT}/celt/celt_lpc.h
${OPUS_ROOT}/celt/cpu_support.h
${OPUS_ROOT}/celt/cwrs.c
${OPUS_ROOT}/celt/cwrs.h
${OPUS_ROOT}/celt/ecintrin.h
${OPUS_ROOT}/celt/entcode.c
${OPUS_ROOT}/celt/entcode.h
${OPUS_ROOT}/celt/entdec.c
${OPUS_ROOT}/celt/entdec.h
${OPUS_ROOT}/celt/entenc.c
${OPUS_ROOT}/celt/entenc.h
${OPUS_ROOT}/celt/fixed_debug.h
${OPUS_ROOT}/celt/fixed_generic.h
${OPUS_ROOT}/celt/float_cast.h
${OPUS_ROOT}/celt/kiss_fft.c
${OPUS_ROOT}/celt/kiss_fft.h
${OPUS_ROOT}/celt/laplace.c
${OPUS_ROOT}/celt/laplace.h
${OPUS_ROOT}/celt/mathops.c
${OPUS_ROOT}/celt/mathops.h
${OPUS_ROOT}/celt/mdct.c
${OPUS_ROOT}/celt/mdct.h
${OPUS_ROOT}/celt/mfrngcod.h
${OPUS_ROOT}/celt/modes.c
${OPUS_ROOT}/celt/modes.h
#${OPUS_ROOT}/celt/opus_custom_demo.c
${OPUS_ROOT}/celt/os_support.h
${OPUS_ROOT}/celt/pitch.c
${OPUS_ROOT}/celt/pitch.h
${OPUS_ROOT}/celt/quant_bands.c
${OPUS_ROOT}/celt/quant_bands.h
${OPUS_ROOT}/celt/rate.c
${OPUS_ROOT}/celt/rate.h
${OPUS_ROOT}/celt/stack_alloc.h
${OPUS_ROOT}/celt/static_modes_fixed.h
${OPUS_ROOT}/celt/static_modes_fixed_arm_ne10.h
${OPUS_ROOT}/celt/static_modes_float.h
${OPUS_ROOT}/celt/static_modes_float_arm_ne10.h
${OPUS_ROOT}/celt/vq.c
${OPUS_ROOT}/celt/vq.h
${OPUS_ROOT}/celt/_kiss_fft_guts.h

${CELT_ARCH_SPECIFIC_FILES}

${OPUS_ROOT}/silk/A2NLSF.c
${OPUS_ROOT}/silk/ana_filt_bank_1.c
${OPUS_ROOT}/silk/API.h
${OPUS_ROOT}/silk/biquad_alt.c
${OPUS_ROOT}/silk/bwexpander.c
${OPUS_ROOT}/silk/bwexpander_32.c
${OPUS_ROOT}/silk/check_control_input.c
${OPUS_ROOT}/silk/CNG.c
${OPUS_ROOT}/silk/code_signs.c
${OPUS_ROOT}/silk/control.h
${OPUS_ROOT}/silk/control_audio_bandwidth.c
${OPUS_ROOT}/silk/control_codec.c
${OPUS_ROOT}/silk/control_SNR.c
${OPUS_ROOT}/silk/debug.c
${OPUS_ROOT}/silk/debug.h
${OPUS_ROOT}/silk/decoder_set_fs.c
${OPUS_ROOT}/silk/decode_core.c
${OPUS_ROOT}/silk/decode_frame.c
${OPUS_ROOT}/silk/decode_indices.c
${OPUS_ROOT}/silk/decode_parameters.c
${OPUS_ROOT}/silk/decode_pitch.c
${OPUS_ROOT}/silk/decode_pulses.c
${OPUS_ROOT}/silk/dec_API.c
${OPUS_ROOT}/silk/define.h
${OPUS_ROOT}/silk/encode_indices.c
${OPUS_ROOT}/silk/encode_pulses.c
${OPUS_ROOT}/silk/enc_API.c
${OPUS_ROOT}/silk/errors.h
${OPUS_ROOT}/silk/gain_quant.c
${OPUS_ROOT}/silk/HP_variable_cutoff.c
${OPUS_ROOT}/silk/init_decoder.c
${OPUS_ROOT}/silk/init_encoder.c
${OPUS_ROOT}/silk/Inlines.h
${OPUS_ROOT}/silk/inner_prod_aligned.c
${OPUS_ROOT}/silk/interpolate.c
${OPUS_ROOT}/silk/lin2log.c
${OPUS_ROOT}/silk/log2lin.c
${OPUS_ROOT}/silk/LPC_analysis_filter.c
${OPUS_ROOT}/silk/LPC_fit.c
${OPUS_ROOT}/silk/LPC_inv_pred_gain.c
${OPUS_ROOT}/silk/LP_variable_cutoff.c
${OPUS_ROOT}/silk/MacroCount.h
${OPUS_ROOT}/silk/MacroDebug.h
${OPUS_ROOT}/silk/macros.h
${OPUS_ROOT}/silk/main.h
${OPUS_ROOT}/silk/NLSF2A.c
${OPUS_ROOT}/silk/NLSF_decode.c
${OPUS_ROOT}/silk/NLSF_del_dec_quant.c
${OPUS_ROOT}/silk/NLSF_encode.c
${OPUS_ROOT}/silk/NLSF_stabilize.c
${OPUS_ROOT}/silk/NLSF_unpack.c
${OPUS_ROOT}/silk/NLSF_VQ.c
${OPUS_ROOT}/silk/NLSF_VQ_weights_laroia.c
${OPUS_ROOT}/silk/NSQ.c
${OPUS_ROOT}/silk/NSQ.h
${OPUS_ROOT}/silk/NSQ_del_dec.c
${OPUS_ROOT}/silk/pitch_est_defines.h
${OPUS_ROOT}/silk/pitch_est_tables.c
${OPUS_ROOT}/silk/PLC.c
${OPUS_ROOT}/silk/PLC.h
${OPUS_ROOT}/silk/process_NLSFs.c
${OPUS_ROOT}/silk/quant_LTP_gains.c
${OPUS_ROOT}/silk/resampler.c
${OPUS_ROOT}/silk/resampler_down2.c
${OPUS_ROOT}/silk/resampler_down2_3.c
${OPUS_ROOT}/silk/resampler_private.h
${OPUS_ROOT}/silk/resampler_private_AR2.c
${OPUS_ROOT}/silk/resampler_private_down_FIR.c
${OPUS_ROOT}/silk/resampler_private_IIR_FIR.c
${OPUS_ROOT}/silk/resampler_private_up2_HQ.c
${OPUS_ROOT}/silk/resampler_rom.c
${OPUS_ROOT}/silk/resampler_rom.h
${OPUS_ROOT}/silk/resampler_structs.h
${OPUS_ROOT}/silk/shell_coder.c
${OPUS_ROOT}/silk/sigm_Q15.c
${OPUS_ROOT}/silk/SigProc_FIX.h
${OPUS_ROOT}/silk/sort.c
${OPUS_ROOT}/silk/stereo_decode_pred.c
${OPUS_ROOT}/silk/stereo_encode_pred.c
${OPUS_ROOT}/silk/stereo_find_predictor.c
${OPUS_ROOT}/silk/stereo_LR_to_MS.c
${OPUS_ROOT}/silk/stereo_MS_to_LR.c
${OPUS_ROOT}/silk/stereo_quant_pred.c
${OPUS_ROOT}/silk/structs.h
${OPUS_ROOT}/silk/sum_sqr_shift.c
${OPUS_ROOT}/silk/tables.h
${OPUS_ROOT}/silk/tables_gain.c
${OPUS_ROOT}/silk/tables_LTP.c
${OPUS_ROOT}/silk/tables_NLSF_CB_NB_MB.c
${OPUS_ROOT}/silk/tables_NLSF_CB_WB.c
${OPUS_ROOT}/silk/tables_other.c
${OPUS_ROOT}/silk/tables_pitch_lag.c
${OPUS_ROOT}/silk/tables_pulses_per_block.c
${OPUS_ROOT}/silk/table_LSF_cos.c
${OPUS_ROOT}/silk/tuning_parameters.h
${OPUS_ROOT}/silk/typedef.h
${OPUS_ROOT}/silk/VAD.c
${OPUS_ROOT}/silk/VQ_WMat_EC.c

${OPUS_ROOT}/silk/float/apply_sine_window_FLP.c
${OPUS_ROOT}/silk/float/autocorrelation_FLP.c
${OPUS_ROOT}/silk/float/burg_modified_FLP.c
${OPUS_ROOT}/silk/float/bwexpander_FLP.c
${OPUS_ROOT}/silk/float/corrMatrix_FLP.c
${OPUS_ROOT}/silk/float/encode_frame_FLP.c
${OPUS_ROOT}/silk/float/energy_FLP.c
${OPUS_ROOT}/silk/float/find_LPC_FLP.c
${OPUS_ROOT}/silk/float/find_LTP_FLP.c
${OPUS_ROOT}/silk/float/find_pitch_lags_FLP.c
${OPUS_ROOT}/silk/float/find_pred_coefs_FLP.c
${OPUS_ROOT}/silk/float/inner_product_FLP.c
${OPUS_ROOT}/silk/float/k2a_FLP.c
${OPUS_ROOT}/silk/float/LPC_analysis_filter_FLP.c
${OPUS_ROOT}/silk/float/LPC_inv_pred_gain_FLP.c
${OPUS_ROOT}/silk/float/LTP_analysis_filter_FLP.c
${OPUS_ROOT}/silk/float/LTP_scale_ctrl_FLP.c
${OPUS_ROOT}/silk/float/main_FLP.h
${OPUS_ROOT}/silk/float/noise_shape_analysis_FLP.c
${OPUS_ROOT}/silk/float/pitch_analysis_core_FLP.c
${OPUS_ROOT}/silk/float/process_gains_FLP.c
${OPUS_ROOT}/silk/float/regularize_correlations_FLP.c
${OPUS_ROOT}/silk/float/residual_energy_FLP.c
${OPUS_ROOT}/silk/float/scale_copy_vector_FLP.c
${OPUS_ROOT}/silk/float/scale_vector_FLP.c
${OPUS_ROOT}/silk/float/schur_FLP.c
${OPUS_ROOT}/silk/float/SigProc_FLP.h
${OPUS_ROOT}/silk/float/sort_FLP.c
${OPUS_ROOT}/silk/float/structs_FLP.h
${OPUS_ROOT}/silk/float/warped_autocorrelation_FLP.c
${OPUS_ROOT}/silk/float/wrappers_FLP.c

${SILK_ARCH_SPECIFIC_FILES}
)

# Group source files
source_group(TREE ${OPUS_ROOT} FILES ${OPUS_SRC_FILES})

# Create Opus lib
add_library(Opus STATIC ${OPUS_SRC_FILES})


target_include_directories(Opus PRIVATE ${OPUS_ROOT})
target_include_directories(Opus PUBLIC  ${OPUS_ROOT}/include)
target_include_directories(Opus PRIVATE ${OPUS_ROOT}/src)
target_include_directories(Opus PRIVATE ${OPUS_ROOT}/celt)
target_include_directories(Opus PRIVATE ${OPUS_ROOT}/silk)
target_include_directories(Opus PRIVATE ${OPUS_ROOT}/silk/float)


target_compile_definitions(Opus PRIVATE -DOPUS_BUILD)
target_compile_definitions(Opus PRIVATE -DENABLE_HARDENING)
target_compile_definitions(Opus PRIVATE -DUSE_ALLOCA)
target_compile_definitions(Opus PRIVATE -DCPU_INFO_BY_C)

if(TARGET_ARM64)
target_compile_definitions(Opus PRIVATE -DOPUS_ARM_ASM)
target_compile_definitions(Opus PRIVATE -DOPUS_ARM_MAY_HAVE_NEON_INTR)
target_compile_definitions(Opus PRIVATE -DOPUS_ARM_PRESUME_AARCH64_NEON_INTR)
else()
target_compile_definitions(Opus PRIVATE -DOPUS_HAVE_RTCD) # Runtime CPU detection
target_compile_definitions(Opus PRIVATE -DOPUS_X86_MAY_HAVE_SSE)
target_compile_definitions(Opus PRIVATE -DOPUS_X86_PRESUME_SSE)
target_compile_definitions(Opus PRIVATE -DOPUS_X86_MAY_HAVE_SSE2)
target_compile_definitions(Opus PRIVATE -DOPUS_X86_PRESUME_SSE2)
target_compile_definitions(Opus PRIVATE -DOPUS_X86_MAY_HAVE_SSE4_1)
target_compile_definitions(Opus PRIVATE -DOPUS_X86_PRESUME_SSE4_1)
target_compile_definitions(Opus PRIVATE -DOPUS_X86_MAY_HAVE_AVX)
endif()

target_compile_definitions(Opus PRIVATE -DPACKAGE_VERSION=\"1.3.1\")
#target_compile_definitions(Opus PUBLIC -DHAVE_CONFIG_H)


