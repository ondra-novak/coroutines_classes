#pragma once
#ifndef SRC_COCLASSES_SRC_COCLASSES_DEBUG_H_
#define SRC_COCLASSES_SRC_COCLASSES_DEBUG_H_
#include <exception>
#include <string>



#ifndef NDEBUG
#ifdef _WIN32
#include <debugapi.h>
#else
#include <unistd.h>
#include <cxxabi.h>
#endif
#endif

namespace cocls {

class unhandled_exception_reporter {
public:
    
    virtual ~unhandled_exception_reporter() = default; 
    virtual void report_exception(std::exception_ptr ptr, const std::type_info &task_type) {
#ifndef NDEBUG
        std::string msg;
        msg.append("cocls: unhandled exception in coroutine: ");
#ifdef _WIN32
        msg.append(task_type.name());
#else
        int status = -1;
        char *demangled_name = abi::__cxa_demangle(task_type.name(), NULL, NULL, &status);
        msg.append(demangled_name);
        free(demangled_name);
#endif
        msg.append(" - ");
        try {
            std::rethrow_exception(ptr);
        } catch (std::exception &e) {
            msg.append("what: ");
            msg.append(e.what());
        } catch (...) {
            msg.append("<non-standard exception>");            
        }
        msg.append("\r\n");
#ifdef _WIN32
        OutputDebugStringA(msg.c_str());        
#else
        ::write(2, msg.data(), msg.size());
#endif
#endif
    }
    
    static void set_instance(unhandled_exception_reporter *inst) {
        get_instance_ptr() = inst;
    }
    static unhandled_exception_reporter &get_instance() {
        return *get_instance_ptr();
    }
    
    
    
    
protected:
    
    static unhandled_exception_reporter *& get_instance_ptr() {
        static unhandled_exception_reporter inst;
        static unhandled_exception_reporter *inst_ptr = &inst;
        return inst_ptr;
    }
};



}



#endif /* SRC_COCLASSES_SRC_COCLASSES_DEBUG_H_ */
