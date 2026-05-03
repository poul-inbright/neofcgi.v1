project("neofcgi.v1")
	kind("StaticLib")
	language("C")
	objdir("build/obj")
	location("build")
	targetdir("build/bin")

	includedirs({
		"include"
	})

	files({
		"include/**.h",
		"src/**.c"
	})

    filter("Configurations:Debug")
        symbols("on")

    filter("Configurations:Release")

	filter "system:windows"
		links({"ws2_32", "kernel32"})