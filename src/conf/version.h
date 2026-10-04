#pragma once

#include "../spi/interface.h"

#define LEBINKPROXY_VERSION  L"2.0.0.16"
#define LEBINKPROXY_BUILDTM  __DATE__ " " __TIME__

#ifdef ASI_DEBUG
#define LEBINKPROXY_BUILDMD  L"DEBUG"
#else
#define LEBINKPROXY_BUILDMD  L"RELEASE"
#endif

#define ASI_SPI_VERSION SPI_VERSION_LATEST
