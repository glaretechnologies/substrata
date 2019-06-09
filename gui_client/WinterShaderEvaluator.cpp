/*=====================================================================
WinterShaderEvaluator.cpp
-------------------------
Copyright Glare Technologies Limited 2017 -
=====================================================================*/
#include "WinterShaderEvaluator.h"


#include <wnt_MathsFuncs.h>
#include <utils/Exception.h>
#include <utils/StringUtils.h>
#include <utils/Timer.h>
#include <utils/ConPrint.h>


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


static void checkFunctionBounds(const Winter::FunctionDefinitionRef& func)
{
	const int MAX_TIME_BOUND = 1 << 16;
	const int MAX_STACK_SIZE = 1 << 12;
	const int MAX_HEAP_SIZE  = 1 << 12;

	// Check time bound
	{
		Winter::GetTimeBoundParams params;
		params.max_bound_computation_steps = MAX_TIME_BOUND;
		const size_t time_bound = func->getTimeBound(params);
		if(time_bound > MAX_TIME_BOUND)
			throw Indigo::Exception(func->sig.name + " time bound was too large: " + toString(time_bound) + 
				", max acceptable bound is " + toString(MAX_TIME_BOUND));
	}

	// Check space bounds
	{
		Winter::GetSpaceBoundParams params;
		params.max_bound_computation_steps = myMax(MAX_STACK_SIZE, MAX_HEAP_SIZE);
		const Winter::GetSpaceBoundResults space_bound = func->getSpaceBound(params);
				
		if(space_bound.stack_space > MAX_STACK_SIZE)
			throw Indigo::Exception(func->sig.name + " stack space bound was too large: " + toString(space_bound.stack_space) + 
				" B, max acceptable bound is " + toString(MAX_STACK_SIZE) + " B");
				
		if(space_bound.heap_space > MAX_HEAP_SIZE)
			throw Indigo::Exception(func->sig.name + " heap space bound was too large: " + toString(space_bound.heap_space) + 
				" B, max acceptable bound is " + toString(MAX_HEAP_SIZE) + " B");
	}
}


WinterShaderEvaluator::WinterShaderEvaluator(const std::string& base_cyberspace_path, const std::string& shader)
:	vm(NULL),
	jitted_evalRotation(NULL),
	jitted_evalTranslation(NULL)
{
	Timer timer;
	
	std::string error_msg;
	Winter::BufferPosition error_pos(NULL, 0, 0);
	build(base_cyberspace_path, shader, this->vm, jitted_evalRotation, jitted_evalTranslation, error_msg, error_pos);
	if(!error_msg.empty())
		throw Indigo::Exception(error_msg);
}


WinterShaderEvaluator::~WinterShaderEvaluator()
{
}


void WinterShaderEvaluator::build(const std::string& base_cyberspace_path, const std::string& shader,
	Winter::VirtualMachineRef& vm_out,
	EVAL_ROTATION_TYPE& jitted_evalRotation_out,
	EVAL_TRANSLATION_TYPE& jitted_evalTranslation_out,
	std::string& error_out,
	Winter::BufferPosition& error_pos_out)
{
	try
	{
		Winter::VMConstructionArgs vm_args;
		vm_args.floating_point_literals_default_to_double = false;
		vm_args.try_coerce_int_to_double_first = false;
		vm_args.real_is_double = false;
		Winter::MathsFuncs::appendExternalMathsFuncs(vm_args.external_functions);

		vm_args.source_buffers.push_back(::Winter::SourceBuffer::loadFromDisk(base_cyberspace_path + "/resources/winter_stdlib.txt"));
		vm_args.source_buffers.push_back(new Winter::SourceBuffer("buffer", shader));


		// NOTE: These names are ignored in practice.
		std::vector<std::string> elem_names(2);
		elem_names[0] = "instance_index";
		elem_names[1] = "num_instances";
		Winter::TypeVRef CybWinterEnv_type = new Winter::StructureType("WinterEnv",
			std::vector<Winter::TypeVRef>(2, new Winter::Int()),
			elem_names
		);


		std::vector< ::Winter::TypeVRef> eval_arg_types;
		eval_arg_types.push_back(new Winter::Float());
		eval_arg_types.push_back(CybWinterEnv_type);
		Winter::FunctionSignature evalRotation_sig("evalRotation", eval_arg_types);
		Winter::FunctionSignature evalTranslation_sig("evalTranslation", eval_arg_types);

		vm_args.entry_point_sigs.push_back(evalRotation_sig);
		vm_args.entry_point_sigs.push_back(evalTranslation_sig);

		vm_out = new Winter::VirtualMachine(vm_args);

		//========== Find evalRotation function ===========
		{
			Winter::FunctionDefinitionRef func = vm_out->findMatchingFunction(evalRotation_sig);

			if(func.nonNull())
			{
				if(*func->returnType() != *vec3Type())
					throw Indigo::Exception(func->sig.toString() + "  must return vec3.");

				jitted_evalRotation_out = (EVAL_ROTATION_TYPE)vm_out->getJittedFunction(evalRotation_sig);

				checkFunctionBounds(func); // Check time and space bounds for this function
			}
		}
		//========== Find evalTranslation function ===========
		{
			Winter::FunctionDefinitionRef func = vm_out->findMatchingFunction(evalTranslation_sig);

			if(func.nonNull())
			{
				if(*func->returnType() != *vec3Type())
					throw Indigo::Exception(func->sig.toString() + "  must return vec3.");

				jitted_evalTranslation_out = (EVAL_ROTATION_TYPE)vm_out->getJittedFunction(evalTranslation_sig);

				checkFunctionBounds(func); // Check time and space bounds for this function
			}
		}

	}
	catch(Winter::BaseException& e)
	{
		if(dynamic_cast<Winter::ExceptionWithPosition*>(&e) != NULL)
		{
			error_out = e.what();
			error_pos_out = dynamic_cast<Winter::ExceptionWithPosition*>(&e)->pos();
		}
		else
		{
			error_out = e.what();
		}
	}
}


const Vec4f WinterShaderEvaluator::evalRotation(float time, const CybWinterEnv& env)
{
	Vec4f res;
	this->jitted_evalRotation(&res, time, &env);
	res.x[3] = 0;
	return res;
}


const Vec4f WinterShaderEvaluator::evalTranslation(float time, const CybWinterEnv& env)
{
	Vec4f res;
	this->jitted_evalTranslation(&res, time, &env);
	res.x[3] = 0;
	return res;
}
