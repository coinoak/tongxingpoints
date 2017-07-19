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

##1. generate addr

count = 250
cmd = "./tongxingpointsd getnewaddress true"
cmdDump= "./tongxingpointsd dumpprivkey "
file = openfile("address.txt","a+");
file.write('[')
for i in range(count):
    res = exe(cmd)
    jsonRes = getJson(res)
    cmdTmp = cmdDump + jsonRes['addr']
    resdump = exe(cmdTmp)
    jsondump = getJson(resdump)
    jsonRes['privkey'] = jsondump['privkey']
    jsonRes['minerkey'] = jsondump['minerkey']
    file.write(json.dumps(jsonRes))
    if (i < count-1):
        file.write(',')
file.write(']')
closefile(file)