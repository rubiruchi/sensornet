#!/usr/bin/python
#--------------------------------------------------------------------------------------
# Author:        Matthew Hengeveld
# Date:          Feb 7, 2015
#--------------------------------------------------------------------------------------
# File:          run.py
# Description:   Runs Flask app on dev webserver
#--------------------------------------------------------------------------------------

    
#----------------------------------------------------------------------------------
# Runs Flask app using Flask built in dev webserver
#----------------------------------------------------------------------------------
from app import app

app.run(host='0.0.0.0',port=5000,debug=True)