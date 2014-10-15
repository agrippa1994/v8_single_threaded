#include <v8/v8.h>
#include <v8/v8-platform.h>
#include <v8/libplatform/libplatform.h>

#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <functional>
#include <memory>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "icui18n.lib")
#pragma comment(lib, "icuuc.lib")
#pragma comment(lib, "v8_base.lib")
#pragma comment(lib, "v8_libbase.lib")
#pragma comment(lib, "v8_libplatform.lib")
#pragma comment(lib, "v8_nosnapshot.lib")

#define ENTER_V8(isolate) v8::Isolate::Scope isoalte_scope(isolate); v8::HandleScope handle_scope(isolate);
#define ADD_ARG(isolate, T) std::make_shared<v8::UniquePersistent<v8::Value>>((isolate), (T))

void sleep(const v8::FunctionCallbackInfo<v8::Value>& args);

class CallQueue
{
public:
	struct CallInfo
	{
		typedef std::shared_ptr<v8::UniquePersistent<v8::Value>> Argument;
		typedef std::vector<Argument> Arguments;

		std::string name;
		std::function<Arguments(v8::Isolate *)> args;
		std::function<void(const v8::Local<v8::Value>&)> callback;
	};

	void append(decltype(CallInfo::name) name)
	{
		std::lock_guard<std::mutex> lg(m_mtx);

		CallInfo c;
		c.name = name;

		m_queue.push(c);
	}

	void append(decltype(CallInfo::name) name, decltype(CallInfo::args) args)
	{
		std::lock_guard<std::mutex> lg(m_mtx);

		CallInfo c;
		c.name = name;
		c.args = args;

		m_queue.push(c);
	}

	void append(decltype(CallInfo::name) name, decltype(CallInfo::args) args, decltype(CallInfo::callback) callback)
	{
		std::lock_guard<std::mutex> lg(m_mtx);

		CallInfo c;
		c.name = name;
		c.args = args;
		c.callback = callback;

		m_queue.push(c);
	}
	
	void operator()(v8::Isolate *isolate)
	{
		ENTER_V8(isolate);

		auto global = isolate->GetCurrentContext()->Global();

		CallQueue::CallInfo cs;
		while (next(cs))
		{
			auto function = v8::Local<v8::Function>::Cast(global->Get(v8::String::NewFromUtf8(isolate, cs.name.c_str())));
			if (function.IsEmpty())
				continue;

			std::vector<v8::Local<v8::Value>> data;
			for (auto& arg : cs.args(isolate))
				data.push_back(v8::Local<v8::Value>::New(isolate, *arg.get()));

			v8::Local<v8::Value> retn;
			if (data.size() == 0)
				retn = function->Call(global, 0, {});
			else
				retn = function->Call(global, data.size(), data.data());

			if (!retn.IsEmpty() && cs.callback)
				cs.callback(retn);
		}
	}
private:
	std::mutex m_mtx;
	std::queue<CallInfo> m_queue;

	bool next(CallInfo& c)
	{
		c = CallInfo();

		if (m_queue.empty())
			return false;

		c = m_queue.front();
		m_queue.pop();

		return true;
	}
};

CallQueue g_callStack;

std::string readFileContent(const std::string& path)
{
	std::stringstream ss;
	std::fstream fs(path);

	fs >> ss.rdbuf();
	return ss.str();
}

int ReportError(v8::Isolate *isolate, v8::TryCatch *try_catch = 0)
{
	ENTER_V8(isolate);

	struct __ {
		~__() {
			std::cin.get();
		}
	} _;

	if (try_catch == NULL)
		return 0;

	auto message = try_catch->Message();

	if (message.IsEmpty())
		std::cout << "Fehler: " << std::endl;
	else
		std::cout << "Fehler:  @ " << message->GetLineNumber() << std::endl;

	return 1;
}

void sleep(const v8::FunctionCallbackInfo<v8::Value>& args)
{
	auto isolate = args.GetIsolate();
	ENTER_V8(isolate);

	if (args.Length() < 1)
		return;

	if (!args[0]->IsNumber())
		return;

	std::this_thread::sleep_for(std::chrono::milliseconds(args[0]->ToNumber()->Int32Value()));
}

void print(const v8::FunctionCallbackInfo<v8::Value>& args)
{
	auto isolate = args.GetIsolate();
	ENTER_V8(isolate);

	for (int i = 0; i < args.Length(); i++) {
		if (!args[i]->IsString())
			continue;

		std::cout << *v8::String::Utf8Value(args[i]);
	}

	std::cout << std::endl;
}

void processCalls(const v8::FunctionCallbackInfo<v8::Value>& args)
{
	g_callStack(args.GetIsolate());
}

v8::Local<v8::Context> createContext(v8::Isolate *isolate)
{
	auto global = v8::ObjectTemplate::New(isolate);

	global->Set(isolate, "sleep", v8::FunctionTemplate::New(isolate, sleep));
	global->Set(isolate, "print", v8::FunctionTemplate::New(isolate, print));
	global->Set(isolate, "processCalls", v8::FunctionTemplate::New(isolate, processCalls));

	return v8::Context::New(isolate, NULL, global);
}

void thread(v8::Isolate *isolate)
{
	for (;; std::this_thread::sleep_for(std::chrono::milliseconds(250)))
	{
		g_callStack.append(std::string("test"), [](v8::Isolate *isolate) -> CallQueue::CallInfo::Arguments
		{
			v8::Local<v8::Object> testObject = v8::Object::New(isolate);
			testObject->Set(v8::String::NewFromUtf8(isolate, "member"), v8::Number::New(isolate, 10));

			return {
				ADD_ARG(isolate, v8::Number::New(isolate, 5)),
				ADD_ARG(isolate, v8::String::NewFromUtf8(isolate, "Hallo welt")),
				ADD_ARG(isolate, testObject)
			};
		});
	}
}

int main()
{
	v8::V8::InitializeICU();
	v8::Platform* platform = v8::platform::CreateDefaultPlatform();
	v8::V8::InitializePlatform(platform);
	v8::V8::Initialize();

	char *gcFlags = "--expose-gc";
	v8::V8::SetFlagsFromString(gcFlags, strlen(gcFlags));

	v8::Isolate* isolate = v8::Isolate::New();
	{
		ENTER_V8(isolate);

		auto ctx = v8::Persistent<v8::Context>(isolate, createContext(isolate));
		auto context = v8::Local<v8::Context>::New(isolate, ctx);
		v8::Context::Scope context_scope(context);

		v8::TryCatch try_catch;

		std::string sourceCode = readFileContent("main.js");
		sourceCode.append(R"(

			if(typeof main !== "function" || typeof loop !== "function")
					throw("main or loop not found!");

			main();
			while(true) {
				processCalls();

				if(!loop())
					break;

				if(typeof garbageCollectionAfterLoop === "boolean")
				{
					if(garbageCollectionAfterLoop == true)
						gc();
				}
				
				if(typeof sleepInterval === "number")
					sleep(sleepInterval);
				else
					sleep(5);
			}
		)");

		auto code = v8::String::NewFromUtf8(isolate, sourceCode.c_str());
		auto script = v8::Script::Compile(code);

		std::thread(std::bind(thread, isolate)).detach();

		auto run = script->Run();
		if (run.IsEmpty()) {
			return ReportError(isolate, &try_catch);
		}
	}

	return ReportError(isolate, 0);
}