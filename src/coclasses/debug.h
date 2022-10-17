#pragma once
#ifndef SRC_COCLASSES_SRC_COCLASSES_DEBUG_H_
#define SRC_COCLASSES_SRC_COCLASSES_DEBUG_H_
#include <exception>
#include <string>
#include <vector>



#ifndef NDEBUG
#ifdef _WIN32
#include <debugapi.h>
#else
#include <unistd.h>
#include <cxxabi.h>
#endif
#endif

namespace cocls {

class debug_reporter {
public:
    
    virtual ~debug_reporter() = default;

    static void demangle_type(const std::type_info &type, std::string &msg) {
#ifndef NDEBUG
#ifdef _WIN32
        msg.append(type.name());
#else
        int status = -1;
        char *demangled_name = abi::__cxa_demangle(type.name(), NULL, NULL, &status);
        msg.append(demangled_name);
        free(demangled_name);
#endif
#else
        msg.append(type.name());
#endif
        }

    virtual void report_exception(std::exception_ptr ptr, const std::type_info &task_type) {
#ifndef NDEBUG
        std::string msg;
        msg.append("cocls: unhandled exception in coroutine: ");
        demangle_type(task_type, msg);
        msg.append(" - ");
        try {
            std::rethrow_exception(ptr);
        } catch (std::exception &e) {
            msg.append("what: ");
            msg.append(e.what());
        } catch (...) {
            msg.append("<non-standard exception>");            
        }
        output_debug(std::move(msg));
    }

    static void output_debug(std::string &&msg) {
#ifdef _WIN32
        OutputDebugStringA(msg.c_str());        
        msg.append("\r\n");
#else
        msg.append("\n");
        ::write(2, msg.data(), msg.size());
#endif
#endif
    }
    virtual void reusable_alloc_size(std::size_t sz, const std::type_info &task_type, const std::vector<const std::type_info *> &args) {
#ifndef NDEBUG
        std::string msg;
        msg.append("II coro-static alloc: ");
        msg.append(std::to_string(sz));
        msg.append(" bytes - ");
        demangle_type(task_type, msg);
        msg.append(" - (");
        bool sep = false;
        for (const std::type_info *c: args) {
            if (sep) msg.append(", "); else sep = true;
            demangle_type(*c, msg);
        }
        msg.append(")");
        output_debug(std::move(msg));
#endif
    }
    
    static void set_instance(debug_reporter *inst) {
        get_instance_ptr() = inst;
    }
    static debug_reporter &get_instance() {
        return *get_instance_ptr();
    }
    
    
    
    
protected:
    
    static debug_reporter *& get_instance_ptr() {
        static debug_reporter inst;
        static debug_reporter *inst_ptr = &inst;
        return inst_ptr;
    }
};



}



#endif /* SRC_COCLASSES_SRC_COCLASSES_DEBUG_H_ */
