/*
 * MockAtscppapi.cc
 *
 *  Created on: Mar 15, 2015
 *      Author: sdavu
 */


#include "MockAtscppapi.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

extern "C" void TSDebug(const char* tag, const char* format_str, ...) {
	char buffer[4096];
	va_list ap;
	va_start(ap, format_str);
	vsnprintf(buffer, sizeof (buffer), format_str, ap);
	std::cout << "[" << tag << "] (" << pthread_self() << ") " << buffer << std::endl;
	va_end(ap);
}

extern "C" void TSError(const char* tag, const char* format_str, ...) {
  char buffer[4096];
  va_list ap;
  va_start(ap, format_str);
  vsnprintf(buffer, sizeof (buffer), format_str, ap);
  std::cout << "[" << tag << "] (" << pthread_self() << ") " << buffer << std::endl;
  va_end(ap);
}

void
atscppapi::RegisterGlobalPlugin(std::string name, std::string vendor, std::string email)
{
 std::cout << "RegisterGlobalPlugin is initialized" << std::endl;
}

MockAtscppapi::MockAtscppapi()
{
	::testing::GTEST_FLAG(throw_on_failure) = true;

	int argc = 1;
	const char *argv = "";

	::testing::InitGoogleTest(&argc, (char **)&argv);
	//::testing::FLAGS_gmock_verbose = "info";
}

MockAtscppapi::~MockAtscppapi()
{

}


