#pragma once
#include "auth_key.h"

// Re-export auth_key functions for use by controllers
#define auth_key_init auth_key_init
#define auth_key_get_private_pem auth_key_get_private_pem
