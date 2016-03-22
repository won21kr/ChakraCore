var bx = new ArrayBuffer(16);
var by = bx;

var bz = new ArrayBuffer(16);

var viewx = new Int32Array(bx);
var viewy = new Int8Array(by, 4, 12);

var viewz = new Float32Array(bz);

for(var i = 0; i < viewx.length; ++i) 
{
    viewx[i] = i + 1;
}

for(var i = 0; i < viewz.length; ++i) 
{
    viewz[i] = i / 2.0;
}

WScript.SetTimeout(testFunction, 50);

/////////////////

function testFunction()
{
    ttdTestReport("viewx.length", viewx.length, 4);
    ttdTestReport("viewy.length", viewy.length, 12);
    ttdTestReport("bx === by", bx === by, true);
    ttdTestReport("viewx.buffer === viewy.buffer", viewx.buffer === viewy.buffer, true);

    var allokx = true;
    for(var i = 0; i < viewx.length; ++i) 
    {
        allokx = allokx && (viewx[i] === i + 1);
    }
    ttdTestReport("allokx", allokx, true);

    ttdTestReport("viewz.length", viewz.length, 4);

    var allokz = true;
    for(var i = 0; i < viewz.length; ++i) 
    {
        allokz = allokz && (viewz[i] === i / 2.0);
    }
    ttdTestReport("allokz", allokz, true);

    ////
    viewx[1] = 0;
    ////

    ttdTestReport("viewy[0]", viewy[0], 0);
    ttdTestReport("viewy[1]", viewy[1], 0);
    ttdTestReport("viewy[2]", viewy[2], 0);
    ttdTestReport("viewy[3]", viewy[3], 0);

    ////
    viewz[0] = 0.5;
    ////

    ttdTestReport("viewz[0]", viewz[0], 0.5);
        
    if(this.ttdTestsFailed)
    {
        ttdTestWrite("Failures!");
    }
    else
    {
        ttdTestWrite("All tests passed!");   
    }
}