#pragma once
#include "tdport/utils/Status.h"
#include "tdport/utils/SliceBuilder.h"

// Simple helpers for early error-return patterns that appear often.
// Examples:
//   RETURN_ERROR_IF(!ptr, "null ptr");
//   RETURN_ERROR("something bad");

#define RETURN_ERROR(message) return ::td::Status::Error(message)

#define RETURN_ERROR_IF(cond, message) \
  do {                                 \
    if (cond) {                        \
      RETURN_ERROR(message);           \
    }                                  \
  } while (0)
