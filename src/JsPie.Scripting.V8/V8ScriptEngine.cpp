#include "stdafx.h"

using namespace System;
using namespace System::Collections::Generic;
using namespace System::Runtime::InteropServices;
using namespace JsPie::Core::Util;

namespace JsPie { namespace Scripting { namespace V8 {

	static V8ScriptEngine::V8ScriptEngine()
	{
		// Initialize V8.
		v8::V8::InitializeICU();
		//v8::V8::InitializeExternalStartupData(argv[0]);
		_pPlatform = v8::platform::CreateDefaultPlatform();
		v8::V8::InitializePlatform(_pPlatform);
		v8::V8::Initialize();
	}

	V8ScriptEngine::V8ScriptEngine(IScriptEnvironment^ environment)
	{
		Guard::NotNull(environment, "environment", nullptr);

		_oEnvironment = environment;

		_pArrayBufferAllocator = new ArrayBufferAllocator();
		v8::Isolate::CreateParams create_params;
		create_params.array_buffer_allocator = _pArrayBufferAllocator;
		_pIsolate = v8::Isolate::New(create_params);
		_oOutputEvents = gcnew List<ControlEvent^>();

		environment->ControlState->OutputEvent += gcnew ControlEventHandler(this, &V8ScriptEngine::OnControlEvent);
	}

	V8ScriptEngine::~V8ScriptEngine()
	{
		_oEnvironment->ControlState->OutputEvent -= gcnew ControlEventHandler(this, &V8ScriptEngine::OnControlEvent);

		if (_pIsolate)
			_pIsolate->Dispose();

		if (_pArrayBufferAllocator)
			delete _pArrayBufferAllocator;

		if (_pData)
			delete _pData;
	}

	ScriptOutcome^ V8ScriptEngine::Initialize()
	{	
		// TODO: Check if already initialized

		v8::Isolate::Scope isolate_scope(_pIsolate);
		v8::HandleScope handle_scope(_pIsolate);

		_pData = new V8ScriptEngineData(_oEnvironment);
		_pIsolate->SetData(0, _pData);

		ScriptResource^ scriptResource = _oEnvironment->Repository->GetMainScript();

		auto source = Util::ToV8String(_pIsolate, scriptResource->Content);
		auto name = Util::ToV8String(_pIsolate, scriptResource->Name);

		auto global = _pData->CreateGlobalTemplate(_pIsolate);

		auto context = v8::Context::New(_pIsolate, NULL, global);

		v8::Context::Scope context_scope(context);
		auto script = v8::Script::Compile(source, name);

		_pData->hContext.Set(_pIsolate, context);
		_pData->hMainScript.Set(_pIsolate, script);
		
		return ScriptOutcome::Success();
	}

	ScriptResult<IScriptOutput^>^ V8ScriptEngine::Run(IScriptInput^ input)
	{
		Guard::NotNull(input, "input", nullptr);

		auto e = input->ControlEvent;
		if (e != nullptr)
		{
			ScriptConsoleExtensions::TraceFormat(_oEnvironment->Console, "Input event: {0} = {1}", e->ControlId, e->Value);
			_oEnvironment->ControlState->ApplyInputEvent(e);
		}

		auto es = input->ControlEvents;
		if (es != nullptr)
		{
			for each (auto ev in es)
			{
				ScriptConsoleExtensions::TraceFormat(_oEnvironment->Console, "Input event: {0} = {1}", ev->ControlId, ev->Value);
				_oEnvironment->ControlState->ApplyInputEvent(ev);
			}
		}

		ScriptConsoleExtensions::Trace(_oEnvironment->Console, "Running script.");

		v8::Isolate::Scope isolate_scope(_pIsolate);
		v8::HandleScope handle_scope(_pIsolate);
		
		auto context = _pData->hContext.Get(_pIsolate);
		auto script = _pData->hMainScript.Get(_pIsolate);

		v8::Context::Scope context_scope(context);
		
		script->Run(context);
		
		// TODO: Check for errors?

		ScriptConsoleExtensions::Trace(_oEnvironment->Console, "Script completed.");

		IScriptOutput^ output = gcnew ScriptOutput(_oOutputEvents);
		_oOutputEvents = gcnew List<ControlEvent^>();

		bool any = false;
		for each (auto outputEvent in output->ControlEvents)
		{
			any = true;
			ScriptConsoleExtensions::TraceFormat(_oEnvironment->Console, "Output event: {0} = {1}", outputEvent->ControlId, outputEvent->Value);
		}

		if (!any)
			ScriptConsoleExtensions::Trace(_oEnvironment->Console, "No output events produced.");

		return ScriptOutcome::Success()->WithValue(output);
	}

	void V8ScriptEngine::OnControlEvent(Object^ sender, ControlEvent^ e)
	{
		_oOutputEvents->Add(e);
	}

} } }