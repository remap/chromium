// #include "third_party/blink/renderer/core/hello/hello.h"

// namespace blink {

// 	HELLO* HELLO::Create() { return new HELLO(); }

// 	HELLO::HELLO() {}

// 	HELLO::~HELLO() {}

// 	String HELLO::sayHello() 
// 	{
// 	    return "Hello, World!\n";
// 	}

// }




#include "third_party/blink/renderer/core/hello/hello.h"

namespace blink {

	// HELLO::HELLO(){}
	HELLO* HELLO::Create() {
		return new HELLO();
	}

	String HELLO::sayHello() 
	{
	    return "Hello, World!";
	}

	void HELLO::setSayHello(String some) {
		return;
	}

	// void HELLO::Trace(blink::Visitor* visitor) {
	// 	ScriptWrappable::Trace(visitor);
	// }

}