The plugins directory is required prebuilt because CMAKE is so lame you can't guarantee that will run

add_custom_command(TARGET "${APP_NAME}" PRE_BUILD
  COMMAND mkdir plugins )
