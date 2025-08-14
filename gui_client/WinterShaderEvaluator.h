/*=====================================================================
WinterShaderEvaluator.h
-----------------------
Copyright Glare Technologies Limited 2017 -
=====================================================================*/
#pragma once


#include <maths/Vec4f.h>
#include <utils/ThreadSafeRefCounted.h>
#if !defined(EMSCRIPTEN)
#include <VirtualMachine.h>
#include <wnt_FunctionSignature.h>
#include <wnt_Type.h>
#include <LanguageTests.h>
#endif
#include "../../dll/IndigoStringUtils.h"


struct CybWinterEnv
{
	int instance_index;
	int num_instances;
};

/*=====================================================================
WinterShaderEvaluator
---------------------

=====================================================================*/
class WinterShaderEvaluator : public ThreadSafeRefCounted
{
public:
	WinterShaderEvaluator(const std::string& base_cyberspace_path, const std::string& shader);
	virtual ~WinterShaderEvaluator();

#if !defined(EMSCRIPTEN)
	typedef void (WINTER_JIT_CALLING_CONV * EVAL_ROTATION_TYPE)(/*return value=*/Vec4f*, float time, const CybWinterEnv* env);
	typedef void (WINTER_JIT_CALLING_CONV * EVAL_TRANSLATION_TYPE)(/*return value=*/Vec4f*, float time, const CybWinterEnv* env);

	static void build(const std::string& base_cyberspace_path, const std::string& shader,
		Winter::VirtualMachineRef& vm_out,
		EVAL_ROTATION_TYPE& jitted_evalRotation_out,
		EVAL_TRANSLATION_TYPE& jitted_evalTranslation_out,
		std::string& error_out,
		Winter::BufferPosition& error_pos_out
	);
	
	const Vec4f evalRotation(float time, const CybWinterEnv& env);
	const Vec4f evalTranslation(float time, const CybWinterEnv& env);

	
	EVAL_ROTATION_TYPE jitted_evalRotation;
	EVAL_TRANSLATION_TYPE jitted_evalTranslation;
private:
	Winter::VirtualMachineRef vm;

#endif
};
