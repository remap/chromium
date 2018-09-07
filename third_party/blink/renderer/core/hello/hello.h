// #ifndef HELLO_H
// #define HELLO_H

// #include "third_party/blink/renderer/platform/bindings/script_wrappable.h"

// namespace blink {
// 	class CORE_EXPORT HELLO final : public GarbageCollectedFinalized<HELLO> {
// 		public:
// 			static HELLO* Create();
//   			~HELLO();
// 			String sayHello();

// 		private:
// 			HELLO();
// 	}
// }

// #endif




#ifndef HELLO_H
#define HELLO_H

#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
// #include "third_party/blink/renderer/bindings/core/v8/exception_state.h"
// #include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {
	class CORE_EXPORT HELLO final : public ScriptWrappable {
		DEFINE_WRAPPERTYPEINFO();
	public:
		static HELLO* Create();
		static String sayHello();
		static void setSayHello(String some);
		// void Trace(blink::Visitor*);
	private:
		HELLO() {}
	};
}

#endif