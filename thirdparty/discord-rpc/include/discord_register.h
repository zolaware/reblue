#pragma once

#ifdef __cplusplus
extern "C" {
#endif

inline void Discord_Register(const char *applicationId, const char *command) {
  (void)applicationId;
  (void)command;
}

inline void Discord_RegisterSteamGame(const char *applicationId,
                                     const char *steamId) {
  (void)applicationId;
  (void)steamId;
}

#ifdef __cplusplus
}
#endif

