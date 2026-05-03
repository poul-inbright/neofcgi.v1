workspace("neofcgi_tests")
	configurations({ "Debug", "Release" })

	include("..")

	project("fcgidump")
		kind("ConsoleApp")
		language("C")
		objdir("build/obj")
		location("build")
		targetdir("./")

		files({
			"fcgidump.c"
		})

		includedirs({
			"../include"
		})

		links({
			"neofcgi.v1"
		})

	project("formdata")
		kind("ConsoleApp")
		language("C")
		objdir("build/obj")
		location("build")
		targetdir("./")

		files({
			"formdata.c"
		})

		includedirs({
			"../include"
		})

		links({
			"neofcgi.v1"
		})

        filter("Configurations:Debug")
            symbols("on")

        filter("Configurations:Release")