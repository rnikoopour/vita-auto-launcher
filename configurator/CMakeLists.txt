add_executable(VALConfigurator source/main.c)

target_link_libraries(VALConfigurator
  vita2d
  SceDisplay_stub
  SceGxm_stub
  ScePgf_stub
  SceCtrl_stub
  SceCommonDialog_stub
  SceSysmodule_stub
  SceAppMgr_stub_weak
  SceIofilemgr_stub
  SceLibKernel_stub
  m
  gcc
)

vita_create_self(eboot.bin VALConfigurator UNSAFE)

vita_create_vpk(VALConfigurator.vpk VALC00001 eboot.bin
  VERSION "01.00"
  NAME "Vita Auto Launcher"
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/livearea/contents/bg0.png     sce_sys/livearea/contents/bg0.png
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/livearea/icon0.png            sce_sys/icon0.png
)
