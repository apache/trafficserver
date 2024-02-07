
add_atsplugin(magick magick.cc)
target_link_libraries(magick PRIVATE ImageMagick::Magick++)
