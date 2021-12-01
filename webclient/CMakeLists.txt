# webclient

set(CURRENT_TARGET webclient)

FILE(GLOB webclient "./*.js" "./*.html")

SOURCE_GROUP(webclient FILES ${webclient})

add_custom_target(webclient
    SOURCES ${webclient})
	