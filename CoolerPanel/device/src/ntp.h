#pragma once
// Start SNTP (pool.ntp.org, UTC). time(nullptr) stays near 0 until the first
// sync; platform_epoch_utc() just returns it and the shared UI shows "—".
void ntp_begin();
