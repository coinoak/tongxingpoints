import os
import json

def exe(cmd):
    res=os.popen(cmd,'r').read()
    return res

def getJson(jsonString):
    return json.loads(jsonString)

def openfile(filename, op):
    return file(filename, op)

def closefile(file):
    file.close()

file = openfile("address.txt", "r")
tmps = file.read()
jsonres = getJson(tmps)
cmd='./tongxingpointsd sendtoaddress '
for obj in jsonres :
    cmdTmp = cmd + obj['addr'] + " 20000"
    exe(cmdTmp)

closefile(file)