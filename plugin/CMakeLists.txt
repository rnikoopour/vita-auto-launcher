add_executable(VitaAutoLauncherPlugin main.c)

target_link_libraries(VitaAutoLauncherPlugin
  taihen_stub
  SceLibKernel_stub
  SceKernelThreadMgr_stub
  SceAppMgr_stub_weak
  SceIofilemgr_stub_weak
  SceCtrl_stub
  k
  gcc
)

set_target_properties(VitaAutoLauncherPlugin
  PROPERTIES LINK_FLAGS "-nostdlib"
)

vita_create_self(VitaAutoLauncher.suprx VitaAutoLauncherPlugin
  CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/vita-auto-launcher.yml
)
