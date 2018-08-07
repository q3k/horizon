#!/usr/bin/python

from __future__ import print_function

import os
import struct


version = '0.1.1'
versionstr = '0.1.1'
architecture = '-x64'



# required by install.bat
print(versionstr + architecture)
	

with open('version.wxi', 'w') as wxi:
	wxi.write("<?xml version='1.0' encoding='utf-8'?>\n")
	wxi.write("<!-- do not edit, this file is created by version.py tool any changes will be lost -->\n")
	wxi.write("<Include>\n")
	wxi.write("<?define ProductVersion='" + version + "' ?>\n")
	wxi.write("<?define FullProductName='Horizon EDA " + versionstr + "' ?>\n")

	wxi.write("<?define ProgramFilesFolder='ProgramFiles64Folder' ?>\n")
	wxi.write("<?define Win64='yes' ?>\n")
	wxi.write("<?define InstallerVersion='200' ?>\n")
	wxi.write("<?define Platform='x64' ?>\n")

	wxi.write("</Include>\n")

