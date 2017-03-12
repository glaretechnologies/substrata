/*=====================================================================
WinterShaderEvaluator.cpp
-------------------------
Copyright Glare Technologies Limited 2017 -
=====================================================================*/
#include "WinterShaderEvaluator.h"



#include <utils/Exception.h>


static Winter::TypeVRef vec3Type()
{
	return new Winter::StructureType("vec3",
		std::vector<Winter::TypeVRef>(1, new Winter::VectorType(
			new Winter::Float(),
			4
		)),
		std::vector<std::string>(1, "v")
	);
}


WinterShaderEvaluator::WinterShaderEvaluator(const std::string& base_cyberspace_path, const std::string& shader)
:	vm(NULL),
	jitted_evalRotation(NULL)
{
	try
	{
		Winter::VMConstructionArgs vm_args;
		vm_args.floating_point_literals_default_to_double = false;
		vm_args.try_coerce_int_to_double_first = false;
		vm_args.real_is_double = false;
		//WinterExternalFuncs::appendExternalFuncs(vm_args.external_functions);
		vm_args.source_buffers.push_back(::Winter::SourceBuffer::loadFromDisk(base_cyberspace_path + "/winter_stdlib.txt"));
		vm_args.source_buffers.push_back(::Winter::SourceBufferRef(new Winter::SourceBuffer("buffer", shader)));


		Winter::TypeVRef CybWinterEnv_type = new Winter::StructureType("WinterEnv",
			std::vector<Winter::TypeVRef>(),
			std::vector<std::string>()
		);


		std::vector< ::Winter::TypeVRef> eval_arg_types;
		eval_arg_types.push_back(new Winter::Float());
		eval_arg_types.push_back(CybWinterEnv_type);
		Winter::FunctionSignature evalRotation_sig("evalRotation", eval_arg_types);

	
		vm_args.entry_point_sigs.push_back(evalRotation_sig);

		this->vm = new Winter::VirtualMachine(vm_args);
		
		//========== Find evalRotation function ===========
		{
			Winter::FunctionDefinitionRef func = vm->findMatchingFunction(evalRotation_sig);

			if(func.isNull())
				throw Indigo::Exception("Failed to find function '" + evalRotation_sig.toString() + "'");
		
			if(*func->returnType() != *vec3Type())
				throw Indigo::Exception(func->sig.toString() + "  must return vec3.");

			this->jitted_evalRotation = (EVAL_ROTATION_TYPE)vm->getJittedFunction(evalRotation_sig);
		}
	}
	catch(Winter::BaseException& e)
	{
		throw Indigo::Exception(e.what());
	}
}


WinterShaderEvaluator::~WinterShaderEvaluator()
{
	delete vm;
}


const Vec4f WinterShaderEvaluator::evalRotation(float time)
{
	CybWinterEnv env;
	Vec4f res;
	this->jitted_evalRotation(&res, time, &env);
	res.x[3] = 0;
	return res;
}
