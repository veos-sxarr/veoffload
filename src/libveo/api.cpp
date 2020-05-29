/* Copyright (C) 2017-2018 by NEC Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
/**
 * @file api.cpp
 * @brief VEO API functions
 */
/**
 * @mainpage Introduction
 *
 * VE offloading is a framework to provide accelerator-style programming
 * on Vector Engine (VE).
 * Using VEO, a programmer can execute code on VE and can control the
 * execution from VH main program.
 *
 * This document describes public APIs for VEO.
 * The page "Modules" shows a list of VEO API functions.
 *
 * @author NEC Corporation
 * @copyright 2017-2018. Licensed under the terms of LGPL v2.1.
 */

#include <ve_offload.h>
#include <config.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include "CallArgs.hpp"
#include "ProcHandle.hpp"
#include "VEOException.hpp"
#include "log.hpp"

namespace veo {
namespace api {
// cast helper from C API to C++ implementation
ProcHandle *ProcHandleFromC(veo_proc_handle *h)
{
  return reinterpret_cast<ProcHandle *>(h);
}
ThreadContext *ThreadContextFromC(veo_thr_ctxt *c)
{
  return reinterpret_cast<ThreadContext *>(c);
}
CallArgs *CallArgsFromC(veo_args *a)
{
  return reinterpret_cast<CallArgs *>(a);
}
ThreadContextAttr *ThreadContextAttrFromC(veo_thr_ctxt_attr *ta)
{
  return reinterpret_cast<ThreadContextAttr *>(ta);
}

template <typename T> int veo_args_set_(veo_args *ca, int argnum, T val)
{
  try {
    CallArgsFromC(ca)->set(argnum, val);
    return 0;
  } catch (VEOException &e) {
    VEO_ERROR(nullptr, "failed to set the argument #%d <- %ld: %s",
              argnum, val, e.what());
    return -1;
  }
}
} // namespace veo::api
} // namespace veo

using veo::api::ProcHandleFromC;
using veo::api::ThreadContextFromC;
using veo::api::CallArgsFromC;
using veo::api::veo_args_set_;
using veo::VEOException;
using veo::api::ThreadContextAttrFromC;

// implementation of VEO API functions
/**
 * \defgroup veoapi VEO API
 *
 * VE Offloading API functions.
 * To use VEO API functions, include "ve_offload.h" header.
 */
//@{
/**
 * @brief return the API version of the VE Offload implementation
 *
 * @retval integer value with API version
 */
int veo_api_version()
{
  return VEO_API_VERSION;
}

/**
 * @brief lower level function to create a VE process
 *
 * @param ossock path to VE OS socket
 * @param vedev path to VE device file
 * @param binname VE alternative veorun binary path
 * @return pointer to VEO process handle upon success
 * @retval NULL VE process creation failed.
 */
veo_proc_handle *veo_proc__create(const char *ossock, const char *vedev,
                                  const char *binname)
{
  try {
    auto rv = new veo::ProcHandle(ossock, vedev, binname);
    return rv->toCHandle();
  } catch (VEOException &e) {
    VEO_ERROR(nullptr, "failed to create ProcHandle: %s", e.what());
    errno = e.err();
    return NULL;
  }
}

/**
 * @brief create a VE process with non-default veorun binary
 *
 * A VE process is created on the VE node specified by venode. If
 * venode is -1, a VE process is created on the VE node specified by
 * environment variable VE_NODE_NUMBER. If venode is -1 and
 * environment variable VE_NODE_NUMBER is not set, a VE process is
 * created on the VE node #0
 *
 * A user executes the program which invokes this function through the
 * job scheduler, the value specified by venode will be treated as a
 * logical VE node number. It will be translated into physical VE node
 * number assigned by the job scheduler. If venode is -1, the first VE
 * node of the VE nodes assigned by the job scheduler is used.
 *
 * @param venode VE node number
 * @param veobin VE alternative veorun binary path
 * @return pointer to VEO process handle upon success
 * @retval NULL VE process creation failed.
 */
veo_proc_handle *veo_proc_create_static(int venode, const char *veobin)
{
  if (venode < -1) {
    VEO_ERROR(nullptr, "venode(%d) is an invalid value.", venode);
    return NULL;
  }
  try {
    const char *venodelist = getenv("_VENODELIST");
    if (venodelist != nullptr) {
      // _VENODELIST is set
      std::string str = std::string(venodelist);
      std::vector<std::string> v;
      std::stringstream ss(str);
      std::string buffer;
      char sep = ' ';
      while(std::getline(ss, buffer, sep)) {
        v.push_back(buffer);
      }
      if (venode == -1) {
        const char *venodenum = getenv("VE_NODE_NUMBER");
        if (venodenum != nullptr) {
          // If VE_NODE_NUMBER is set, check is's value is in _VENODELIST
          venode = stoi(std::string(venodenum));
          int found = 0;
          for (unsigned int i=0; i < v.size(); i++) {
            if (stoi(v[i]) == venode) {
              found = 1;
              break;
            }
          }
          if (found == 0) {
            VEO_ERROR(nullptr, "VE node #%d is not assigned by the scheduler",
                      venode);
            return NULL;
          }
        } else {
          // If VE_NODE_NUMBER is not set, use the first value in _VENODELIST
          if (v.size() > 0) 
            venode = stoi(v[0]);
          else { 
            VEO_ERROR(nullptr, "_VENODELIST is empty.", NULL);
            return NULL;
          }
        }
      } else {
	// translate venode using _VENODELIST
        if ((int)v.size() <= venode) {
          VEO_ERROR(nullptr, "venode = %d exceeds the size of _VENDOELIST %d", venode, v.size());
          return NULL;
        }
        venode = stoi(v[venode]);
      }
    } else {
      // _VENODELIST is not set
      if (venode == -1) {
        const char *venodenum = getenv("VE_NODE_NUMBER");
        if (venodenum != nullptr) {
          venode = stoi(std::string(venodenum));
        } else {
          venode = 0;
        }
      }
    }
    char vedev[16];// the size of "/dev/veslot" = 12.
    snprintf(vedev, sizeof(vedev), VE_DEV, venode);
    char ossock[sizeof(VEOS_SOCKET) + 16];
    snprintf(ossock, sizeof(ossock), VEOS_SOCKET, venode);
    return veo_proc__create(ossock, vedev, veobin);
  } catch (std::invalid_argument &e) {
    VEO_ERROR(nullptr, "failed to create proc: %s", e.what());
    return 0;
  } catch (std::out_of_range &e) {
    VEO_ERROR(nullptr, "failed to create proc: %s", e.what());
    return 0;
  }
}
 
/**
 * @brief create a VE process
 *
 * A VE process is created on the VE node specified by venode. If
 * venode is -1, a VE process is created on the VE node specified by
 * environment variable VE_NODE_NUMBER. If venode is -1 and
 * environment variable VE_NODE_NUMBER is not set, a VE process is
 * created on the VE node #0
 *
 * A user executes the program which invokes this function through the
 * job scheduler, the value specified by venode will be treated as a
 * logical VE node number. It will be translated into physical VE node
 * number assigned by the job scheduler. If venode is -1, the first VE
 * node of the VE nodes assigned by the job scheduler is used.
 *
 * @param venode VE node number
 * @return pointer to VEO process handle upon success
 * @retval NULL VE process creation failed.
 */
veo_proc_handle *veo_proc_create(int venode)
{
  const char *veobin = getenv("VEORUN_BIN");
  if (veobin != nullptr)
    return veo_proc_create_static(venode, veobin);
  else
    return veo_proc_create_static(venode, VEORUN_BIN);
}

/**
 * @brief destroy a VE process
 * @param proc pointer to VEO process handle
 * @retval 0 VEO process handle is successfully destroyed.
 * @retval -1 VEO process handle destruction failed.
 */
int veo_proc_destroy(veo_proc_handle *proc)
{
  try {
    ProcHandleFromC(proc)->exitProc();
  } catch (VEOException &e) {
    return -1;
  }
  return 0;
}

/**
 * @brief load a VE library
 * @param proc VEO process handle
 * @param libname a library file name to load
 * @return a handle for the library
 * @retval 0 library loading request failed.
 */
uint64_t veo_load_library(veo_proc_handle *proc, const char *libname)
{
  try {
    return ProcHandleFromC(proc)->loadLibrary(libname);
  } catch (VEOException &e) {
    VEO_ERROR(nullptr, "failed to load library: %s", e.what());
    errno = e.err();
    return 0;
  }
}

/**
 * @brief find a symbol in VE program
 * @param proc VEO process handle
 * @param libhdl a library handle
 * @param symname symbol name to find
 * @return VEMVA of the symbol upon success.
 * @retval 0 failed to find symbol.
 */
uint64_t veo_get_sym(veo_proc_handle *proc, uint64_t libhdl,
                     const char *symname)
{
  try {
    return ProcHandleFromC(proc)->getSym(libhdl, symname);
  } catch (VEOException &e) {
    VEO_ERROR(nullptr, "failed to get symbol: %s", e.what());
    errno = e.err();
    return 0;
  }
}

/**
 * @brief open a VEO context
 *
 * Create a new VEO context, a pseudo thread and VE thread for the context.
 * All attributes which veo_context_open_with_attr() can specify have default value.
 *
 * @param proc VEO process handle
 * @return a pointer to VEO thread context upon success.
 * @retval NULL failed to create a VEO context.
 */
veo_thr_ctxt *veo_context_open(veo_proc_handle *proc)
{
  try {
    veo_thr_ctxt *ctx = ProcHandleFromC(proc)->openContext()->toCHandle();
    auto rv = reinterpret_cast<intptr_t>(ctx);
    if ( rv < 0 ) {
      errno = -rv;
      return NULL;
    }
    return ctx;
  } catch (VEOException &e) {
    VEO_ERROR(nullptr, "failed to open context: %s", e.what());
    errno = e.err();
    return NULL;
  }
}

/**
 * @brief open a VEO context with attributes.
 *
 * Create a new VEO context, a pseudo thread and VE thread for the context.
 *
 * @param proc VEO process handle
 * @param tca veo_thr_ctxt_attr object
 * @return a pointer to VEO thread context upon success.
 * @retval NULL failed to create a VEO context.
 */
veo_thr_ctxt *veo_context_open_with_attr(
				veo_proc_handle *proc, veo_thr_ctxt_attr *tca)
{
  if ((tca == nullptr) || (proc == nullptr)) {
    errno = EINVAL;
    return NULL;
  } else if (ProcHandleFromC(proc)->getVeorunVersion() < VEORUN_VERSION3) {
    errno = ENOSYS;
    return NULL;
  }
  auto attr = ThreadContextAttrFromC(tca);
  try {
    veo_thr_ctxt *ctx = ProcHandleFromC(proc)->openContext(*attr)->toCHandle();
    auto rv = reinterpret_cast<intptr_t>(ctx);
    if ( rv < 0 ) {
      errno = -rv;
      return NULL;
    }
    return ctx;
  } catch (VEOException &e) {
    VEO_ERROR(nullptr, "failed to open context: %s", e.what());
    errno = e.err();
    return NULL;
  }
}

/**
 * @brief close a VEO context
 *
 * @param ctx a VEO context to close
 * @retval 0 VEO context is successfully closed.
 * @retval non-zero failed to close VEO context.
 */
int veo_context_close(veo_thr_ctxt *ctx)
{
  auto c = ThreadContextFromC(ctx);
  if (c->isMainThread()) {
    VEO_ERROR(c, "DO NOT close the main thread %p", c);
    return -EINVAL;
  }
  int rv = c->close();
  if (rv == 0) {
    delete c;
  }
  return rv;
}

/**
 * @brief get VEO context state
 * 
 * @param ctx a VEO context 
 * @return the state of the VEO context state.
 * @retval VEO_STATE_RUNNING VEO context is running.
 * @retval VEO_STATE_SYSCALL VEO context is handling a system call from VE.
 * @retval VEO_STATE_BLOCKED VEO context is blocked.
 * @retval VEO_STATE_EXIT VEO context  exited.
 */
int veo_get_context_state(veo_thr_ctxt *ctx)
{
  return ThreadContextFromC(ctx)->getState();
}

/**
 * @brief request a VE thread to call a function
 *
 * @param ctx VEO context to execute the function on VE.
 * @param addr VEMVA of the function to call
 * @param args arguments to be passed to the function
 * @return request ID
 * @retval VEO_REQUEST_ID_INVALID request failed.
 */
uint64_t veo_call_async(veo_thr_ctxt *ctx, uint64_t addr, veo_args *args)
{
  try {
    return ThreadContextFromC(ctx)->callAsync(addr, *CallArgsFromC(args));
  } catch (VEOException &e) {
    return VEO_REQUEST_ID_INVALID;
  }
}

/**
 * @brief request a VE thread to call a function
 *
 * @param ctx VEO context to execute the function on VE.
 * @param libhdl a library handle
 * @param symname symbol name to find
 * @param args arguments to be passed to the function
 * @return request ID
 * @retval VEO_REQUEST_ID_INVALID request failed.
 */
uint64_t veo_call_async_by_name(veo_thr_ctxt *ctx, uint64_t libhdl,
                        const char *symname, veo_args *args)
{
  try {
    return ThreadContextFromC(ctx)->callAsyncByName(libhdl, symname, *CallArgsFromC(args));
  } catch (VEOException &e) {
    return VEO_REQUEST_ID_INVALID;
  }
}

/**
 * @brief call a VH function asynchronously
 *
 * @param ctx VEO context in which to execute the function.
 * @param func address of VH function to call
 * @param arg pointer to arguments structure for the function
 * @return request ID
 * @retval VEO_REQUEST_ID_INVALID if request failed.
 */
uint64_t veo_call_async_vh(veo_thr_ctxt *ctx, uint64_t (*func)(void *),
                           void *arg)
{
  try {
    return ThreadContextFromC(ctx)->callVHAsync(func, arg);
  } catch (VEOException &e) {
    return VEO_REQUEST_ID_INVALID;
  }
}

/**
 * @brief pick up a resutl from VE function if it has finished
 *
 * @param ctx VEO context
 * @param reqid request ID
 * @param retp pointer to buffer to store the return value from the function.
 * @retval VEO_COMMAND_OK function is successfully returned.
 * @retval VEO_COMMAND_EXCEPTION an exception occurred on function.
 * @retval VEO_COMMAND_ERROR an error occurred on function.
 * @retval VEO_COMMAND_UNFINISHED function is not finished.
 * @retval -1 internal error.
 */
int veo_call_peek_result(veo_thr_ctxt *ctx, uint64_t reqid, uint64_t *retp)
{
  try {
    return ThreadContextFromC(ctx)->callPeekResult(reqid, retp);
  } catch (VEOException &e) {
    return -1;
  }
}

/**
 * @brief pick up a resutl from VE function

 * @param ctx VEO context
 * @param reqid request ID
 * @param retp pointer to buffer to store the return value from the function.
 * @retval VEO_COMMAND_OK function is successfully returned.
 * @retval VEO_COMMAND_EXCEPTION an exception occurred on execution.
 * @retval VEO_COMMAND_ERROR an error occurred on execution.
 * @retval VEO_COMMAND_UNFINISHED function is not finished.
 * @retval -1 internal error.
 */
int veo_call_wait_result(veo_thr_ctxt *ctx, uint64_t reqid, uint64_t *retp)
{
  try {
    return ThreadContextFromC(ctx)->callWaitResult(reqid, retp);
  } catch (VEOException &e) {
    return -1;
  }
}

/**
 * @brief Allocate a VE memory buffer
 *
 * @param h VEO process handle
 * @param addr [out] VEMVA address
 * @param size [in] size in bytes
 * @retval 0 memory allocation succeeded.
 * @retval -1 memory allocation failed.
 * @retval -2 internal error.
 */
int veo_alloc_mem(veo_proc_handle *h, uint64_t *addr, const size_t size)
{
  try {
    *addr = ProcHandleFromC(h)->allocBuff(size);
    if (*addr == 0UL)
      return -1;
  } catch (VEOException &e) {
    return -2;
  }
  return 0;
}

/**
 * @brief Free a VE memory buffer
 *
 * @param h VEO process handle
 * @param addr [in] VEMVA address
 * @retval 0 memory is successfully freed.
 * @retval -1 internal error.
 */
int veo_free_mem(veo_proc_handle *h, uint64_t addr)
{
  try {
    ProcHandleFromC(h)->freeBuff(addr);
  } catch (VEOException &e) {
    return -1;
  }
  return 0;
}

/**
 * @brief Read VE memory
 *
 * @param h VEO process handle
 * @param dst destination VHVA
 * @param src source VEMVA
 * @param size size in byte
 * @return zero upon success; negative upon failure.
 */
int veo_read_mem(veo_proc_handle *h, void *dst, uint64_t src, size_t size)
{
  try {
    return ProcHandleFromC(h)->readMem(dst, src, size);
  } catch (VEOException &e) {
    return -1;
  }
}

/**
 * @brief Write VE memory
 *
 * @param h VEO process handle
 * @param dst destination VEMVA
 * @param src source VHVA
 * @param size size in byte
 * @return zero upon success; negative upon failure.
 */
int veo_write_mem(veo_proc_handle *h, uint64_t dst, const void *src,
                  size_t size)
{
  try {
    return ProcHandleFromC(h)->writeMem(dst, src, size);
  } catch (VEOException &e) {
    return -1;
  }
}

/**
 * @brief Asynchronously read VE memory
 *
 * @param ctx VEO context
 * @param dst destination VHVA
 * @param src source VEMVA
 * @param size size in byte
 * @return request ID
 * @retval VEO_REQUEST_ID_INVALID request failed.
 */
uint64_t veo_async_read_mem(veo_thr_ctxt *ctx, void *dst, uint64_t src,
                            size_t size)
{
  try {
    return ThreadContextFromC(ctx)->asyncReadMem(dst, src, size);
  } catch (VEOException &e) {
    return VEO_REQUEST_ID_INVALID;
  }
}

/**
 * @brief Asynchronously write VE memory
 *
 * @param ctx VEO context
 * @param dst destination VEMVA
 * @param src source VHVA
 * @param size size in byte
 * @return request ID
 * @retval VEO_REQUEST_ID_INVALID request failed.
 */
uint64_t veo_async_write_mem(veo_thr_ctxt *ctx, uint64_t dst, const void *src,
                             size_t size)
{
  try {
    return ThreadContextFromC(ctx)->asyncWriteMem(dst, src, size);
  } catch (VEOException &e) {
    return VEO_REQUEST_ID_INVALID;
  }
}

/**
 * @brief allocate VEO arguments object (veo_args)
 *
 * @return pointer to veo_args
 * @retval NULL the allocation of veo_args failed.
 */
veo_args *veo_args_alloc(void)
{
  try {
    auto rv = new veo::CallArgs();
    return rv->toCHandle();
  } catch (VEOException &e) {
    errno = e.err();
    return NULL;
  }
}

/**
 * @brief set a 64-bit integer argument
 *
 * @param ca veo_args
 * @param argnum the argnum-th argument
 * @param val value to be set
 * @return zero upon success; negative upon failure.
 */
int veo_args_set_i64(veo_args *ca, int argnum, int64_t val)
{
  return veo_args_set_(ca, argnum, val);
}

/**
 * @brief set a 64-bit uunsigned integer argument
 *
 * @param ca veo_args
 * @param argnum the argnum-th argument
 * @param val value to be set
 * @return zero upon success; negative upon failure.
 */
int veo_args_set_u64(veo_args *ca, int argnum, uint64_t val)
{
  return veo_args_set_(ca, argnum, val);
}

/**
 * @brief set a 32-bit integer argument
 *
 * @param ca veo_args
 * @param argnum the argnum-th argument
 * @param val value to be set
 * @return zero upon success; negative upon failure.
 */
int veo_args_set_i32(veo_args *ca, int argnum, int32_t val)
{
  return veo_args_set_(ca, argnum, val);
}

/**
 * @brief set a 32-bit unsigned integer argument
 *
 * @param ca veo_args
 * @param argnum the argnum-th argument
 * @param val value to be set
 * @return zero upon success; negative upon failure.
 */
int veo_args_set_u32(veo_args *ca, int argnum, uint32_t val)
{
  return veo_args_set_(ca, argnum, val);
}

/**
 * @brief set a 16-bit integer argument
 *
 * @param ca veo_args
 * @param argnum the argnum-th argument
 * @param val value to be set
 * @return zero upon success; negative upon failure.
 */
int veo_args_set_i16(veo_args *ca, int argnum, int16_t val)
{
  return veo_args_set_(ca, argnum, val);
}

/**
 * @brief set a 16-bit unsigned integer argument
 *
 * @param ca veo_args
 * @param argnum the argnum-th argument
 * @param val value to be set
 * @return zero upon success; negative upon failure.
 */
int veo_args_set_u16(veo_args *ca, int argnum, uint16_t val)
{
  return veo_args_set_(ca, argnum, val);
}

/**
 * @brief set a 8-bit integer argument
 *
 * @param ca veo_args
 * @param argnum the argnum-th argument
 * @param val value to be set
 * @return zero upon success; negative upon failure.
 */
int veo_args_set_i8(veo_args *ca, int argnum, int8_t val)
{
  return veo_args_set_(ca, argnum, val);
}

/**
 * @brief set a 8-bit unsigned integer argument
 *
 * @param ca veo_args
 * @param argnum the argnum-th argument
 * @param val value to be set
 * @return zero upon success; negative upon failure.
 */
int veo_args_set_u8(veo_args *ca, int argnum, uint8_t val)
{
  return veo_args_set_(ca, argnum, val);
}

/**
 * @brief set a double precision floating point number argument
 *
 * @param ca veo_args
 * @param argnum the argnum-th argument
 * @param val value to be set
 * @return zero upon success; negative upon failure.
 */
int veo_args_set_double(veo_args *ca, int argnum, double val)
{
  return veo_args_set_(ca, argnum, val);
}

/**
 * @brief set a single precision floating point number argument
 *
 * @param ca veo_args
 * @param argnum the argnum-th argument
 * @param val value to be set
 * @return zero upon success; negative upon failure.
 */
int veo_args_set_float(veo_args *ca, int argnum, float val)
{
  return veo_args_set_(ca, argnum, val);
}

/**
 * @brief set VEO function calling argument pointing to buffer on stack
 *
 * @param ca pointer to veo_args object
 * @param inout intent of argument, currently only VEO_INTENT_IN is supported
 * @param argnum argument number that is being set
 * @param buff char pointer to buffer that will be copied to the VE stack
 * @param len length of buffer that is copied to the VE stack
 * @retval  0 argumen is successfully set.
 * @retval -1 an error occurred.
 *
 * The buffer is copied to the stack and will look to the VE callee like a
 * local variable of the caller function. It is currently erased right after
 * the call returns, thus only intent IN args passing is supported. Use this
 * to pass structures to the VE "kernel" function. The size of arguments 
 * passed on the stack is limited to 63MB, since the size of the initial 
 * stack is 64MB. Try staying well below this value, allocate and use memory
 * buffers on heap when you have huge argument arrays to pass.
 */
int veo_args_set_stack(veo_args *ca, enum veo_args_intent inout,
                       int argnum, char *buff, size_t len)
{
  try {
    CallArgsFromC(ca)->setOnStack(inout, argnum, buff, len);
    return 0;
  } catch (VEOException &e) {
    VEO_ERROR(nullptr, "failed set_on_stack CallArgs(%d): %s",
              argnum, e.what());
    return -1;
  }
}

/**
 * @brief clear arguments set in VEO arguments object
 *
 * @param ca veo_args object
 */
void veo_args_clear(veo_args *ca)
{
  CallArgsFromC(ca)->clear();
}

/**
 * @brief free VEO arguments object
 *
 * @param ca veo_args object
 */
void veo_args_free(veo_args *ca)
{
  delete CallArgsFromC(ca);
}

/**
 * @brief VEO version
 *
 * @return pointer to VEO version string
 */
const char *veo_version_string()
{
  return VERSION;
}

/**
 * @brief allocate and initialize VEO thread context attributes object
 *        (veo_thr_ctxt_attr).
 *
 * @return pointer to veo_thr_ctxt_attr
 * @retval NULL the allocation of veo_thr_ctxt_attr failed.
 */
veo_thr_ctxt_attr *veo_alloc_thr_ctxt_attr(void)
{
  try {
    auto rv = new veo::ThreadContextAttr();
    return rv->toCHandle();
  } catch (VEOException &e) {
    errno = e.err();
    return NULL;
  }
}

/**
 * @brief free VEO thread context attributes object.
 *        freeing a VEO thread context attributes object has no effect on VEO
 *        thread contexts that were created using that object.
 *
 * @param tca veo_thr_ctxt_attr object
 * @retval 0 VEO thread context attributes are successfully freed.
 * @retval -1 VEO thread context attributes de-allocation failed.
 */
int veo_free_thr_ctxt_attr(veo_thr_ctxt_attr *tca)
{
  if (tca == nullptr) {
    errno = EINVAL;
    return -1;
  }
  delete ThreadContextAttrFromC(tca);
  return 0;
}

/**
 * @brief set stack size of VE thread which executes a VE function.
 *
 * @param tca veo_thr_ctxt_attr object
 * @param stack_sz stack size of VE thread
 *
 * @return 0 upon success; -1 upon failure.
 */
int veo_set_thr_ctxt_stacksize(veo_thr_ctxt_attr *tca, size_t stack_sz)
{
  if (tca == nullptr) {
    errno = EINVAL;
    return -1;
  }
  try {
    ThreadContextAttrFromC(tca)->setStacksize(stack_sz);
  } catch (VEOException &e) {
    VEO_ERROR(nullptr, "failed veo_set_thr_ctxt_stacksize (%p)", tca);
    errno = e.err();
    return -1;
  }
  return 0;
}

/**
 * @brief get stack size of VE thread which executes a VE function.
 *
 * @param tca veo_thr_ctxt_attr object
 * @param stack_sz pointer to store stack size of VE thread.
 *
 * @return 0 upon success; -1 upon failure.
 */
int veo_get_thr_ctxt_stacksize(veo_thr_ctxt_attr *tca, size_t *stack_sz)
{
  if (tca == nullptr || stack_sz == nullptr) {
    errno = EINVAL;
    return -1;
  }
  *stack_sz = ThreadContextAttrFromC(tca)->getStacksize();
  return 0;
}
//@}