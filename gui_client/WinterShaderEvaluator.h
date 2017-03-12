/*=====================================================================
WinterShaderEvaluator.h
-----------------------
Copyright Glare Technologies Limited 2017 -
=====================================================================*/
#pragma once


#include <maths/Vec4f.h>
#include <VirtualMachine.h>
#include <wnt_FunctionSignature.h>
#include <wnt_Type.h>
#include <LanguageTests.h>
#include "../../lang/WinterExternalFuncs.h"
#include "../../lang/WinterEnv.h"
#include "../../dll/IndigoStringUtils.h"


struct CybWinterEnv
{

};

/*=====================================================================
WinterShaderEvaluator
---------------------

=====================================================================*/
class WinterShaderEvaluator : public RefCounted
{
public:
	WinterShaderEvaluator(const std::string& base_cyberspace_path, const std::string& shader);
	virtual ~WinterShaderEvaluator();

	
	const Vec4f evalRotation(float time);


	typedef void (WINTER_JIT_CALLING_CONV * EVAL_ROTATION_TYPE)(/*return value=*/Vec4f*, float time, CybWinterEnv* env);
	EVAL_ROTATION_TYPE jitted_evalRotation;
private:
	Winter::VirtualMachine* vm;
};
