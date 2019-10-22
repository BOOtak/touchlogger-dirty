//
// Created by k.leyfer on 22.06.2017.
//

#ifndef TOUCHLOGGER_DIRTY_LOGGING_H
#define TOUCHLOGGER_DIRTY_LOGGING_H

#include <android/log.h>

#define LOGV(...) { __android_log_print(ANDROID_LOG_INFO, "dirtycopy", __VA_ARGS__); printf(__VA_ARGS__); printf("\n"); fflush(stdout); }

#endif //TOUCHLOGGER_DIRTY_LOGGING_H