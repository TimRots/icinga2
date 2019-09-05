/* Icinga 2 | (c) 2012 Icinga GmbH | GPLv2+ */

#ifndef IO_ENGINE_H
#define IO_ENGINE_H

#include "base/lazy-init.hpp"
#include <atomic>
#include <exception>
#include <memory>
#include <thread>
#include <vector>
#include <stdexcept>
#include <boost/exception/all.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/spawn.hpp>

namespace icinga
{

/**
 * Scope lock for CPU-bound work done in an I/O thread
 *
 * @ingroup base
 */
class CpuBoundWork
{
public:
	CpuBoundWork(boost::asio::yield_context yc);
	CpuBoundWork(const CpuBoundWork&) = delete;
	CpuBoundWork(CpuBoundWork&&) = delete;
	CpuBoundWork& operator=(const CpuBoundWork&) = delete;
	CpuBoundWork& operator=(CpuBoundWork&&) = delete;
	~CpuBoundWork();

	void Done();

private:
	bool m_Done;
};

/**
 * Scope break for CPU-bound work done in an I/O thread
 *
 * @ingroup base
 */
class IoBoundWorkSlot
{
public:
	IoBoundWorkSlot(boost::asio::yield_context yc);
	IoBoundWorkSlot(const IoBoundWorkSlot&) = delete;
	IoBoundWorkSlot(IoBoundWorkSlot&&) = delete;
	IoBoundWorkSlot& operator=(const IoBoundWorkSlot&) = delete;
	IoBoundWorkSlot& operator=(IoBoundWorkSlot&&) = delete;
	~IoBoundWorkSlot();

private:
	boost::asio::yield_context yc;
};

/**
 * Async I/O engine
 *
 * @ingroup base
 */
class IoEngine
{
	friend CpuBoundWork;
	friend IoBoundWorkSlot;

public:
	IoEngine(const IoEngine&) = delete;
	IoEngine(IoEngine&&) = delete;
	IoEngine& operator=(const IoEngine&) = delete;
	IoEngine& operator=(IoEngine&&) = delete;
	~IoEngine();

	static IoEngine& Get();

	boost::asio::io_service& GetIoService();

private:
	IoEngine();

	void RunEventLoop();

	static LazyInit<std::unique_ptr<IoEngine>> m_Instance;

	boost::asio::io_service m_IoService;
	boost::asio::io_service::work m_KeepAlive;
	std::vector<std::thread> m_Threads;
	boost::asio::deadline_timer m_AlreadyExpiredTimer;
	std::atomic_int_fast32_t m_CpuBoundSemaphore;
};

class TerminateIoThread : public std::exception
{
};

/**
 * Condition variable which doesn't block I/O threads
 *
 * @ingroup base
 */
class AsioConditionVariable
{
public:
	AsioConditionVariable(boost::asio::io_service& io, bool init = false);

	void Set();
	void Clear();
	void Wait(boost::asio::yield_context yc);

private:
	boost::asio::deadline_timer m_Timer;
};

inline boost::exception_ptr convertExceptionPtr(std::exception_ptr ex) {
  try {
    throw boost::enable_current_exception(ex);
  } catch (...) {
    return boost::current_exception();
  }
}

inline void rethrow_boost_compatible_exception_pointer() {
  // Ensure that any exception gets correctly transferred by
  // Boost.Exception's functions to somewhere outside the coroutine
  // (This is needed because Boost.Coroutine relies on Boost.Exception for
  // exception propagation)

  // boost::current_exception does not always work well with exceptions
  // that have not been thrown using boost's boost::throw_exception(e) or
  // with throw(boost::enable_current_exception(e)), hence we use
  // std::current_exception (from the standard library), convert it to a
  // boost::exception_ptr type and rethrow it in a Boost.Exception-compatible
  // way

  std::exception_ptr sep;
  sep = std::current_exception();
  boost::exception_ptr bep = convertExceptionPtr(sep);
  boost::rethrow_exception(bep);
}

template <typename Handler, typename Function>
void spawn_coroutine(Handler h, Function f) {
  auto l_CoroutinesStackSize = 64 * 1024 * 1024;

  boost::asio::spawn(std::forward<Handler>(h),
                     [f](boost::asio::yield_context yield) {
    try {
      f(yield);
    } catch (const boost::coroutines::detail::forced_unwind &) {
      throw;
      // needed for proper stack unwinding when coroutines are destroyed
    } catch (...) {
      rethrow_boost_compatible_exception_pointer();
      // handle uncaught exceptions at the location where io_service.run() is
      // called (typically in "int main()")
    }
  }, boost::coroutines::attributes(l_CoroutinesStackSize));
}

}

#endif /* IO_ENGINE_H */
