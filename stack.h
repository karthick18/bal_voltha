#ifndef _STACK_H_
#define _STACK_H_

#include <iostream>
#include <string>
#include <sstream>

namespace stack {

    template <class T>
        class Stack {
        private:
        std::vector <T> data;
        public:
        inline bool Empty(void) {
            return data.empty();
        }
        inline void Push(T item) {
            data.push_back(item);
        }
        inline void Pop(void) {
            data.pop_back();
        }
        inline T Top(void) {
            return data.back();
        }
    };

    template <class T>
    inline std::ostream & operator <<(std::ostream &out, Stack<T> &s) {
        while(!s.Empty()) {
            out << s.Top();
            s.Pop();
        }
        return out;
    }

    template <class T>
    inline std::ostringstream & operator <<(std::ostringstream &out, Stack<T> &s) {
        while(!s.Empty()) {
            out << s.Top();
            s.Pop();
        }
        return out;
    }
}

#endif
