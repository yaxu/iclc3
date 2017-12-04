var editor
var inHistory = -1
var sliding = false
var changeList = []
var evalList = []
var snapshotInterval = 32
var playback = false
var firstTick = null

var saveData = function (data, fileName) {
    var json = JSON.stringify(data),
        blob = new Blob([json], {type: "octet/stream"}),
        url = window.URL.createObjectURL(blob);
    a = $("#dummylink")[0];
    //console.log("url: " + a.href + " data: " + data)
    a.href = url;
    a.download = fileName;
    a.click();
    window.URL.revokeObjectURL(url);
};

var saveState = function () {
    console.log("save state");
    saveData([evalList,changeList], "edits.json");
    console.log("saved state");
    return(false)
}

var init_editor = function() {
    var connected = false
    // list of list of changes
    var recordChanges= true

    var tidal = function (code) {
	var secs = (new Date().getTime()) / 1000.0
        // console.log("eval: " + code)
	ws.send("/eval " + secs + " " + code)
    }

    var getLastEvalN = function(when) {
	var lastEvalN = -1;
	// console.log("evalList length: " + evalList.length)
    	for (i=0; i < evalList.length; ++i) {
	    var e = evalList[i]
	    // console.log("compare eval @" + e.when + " with " + when)
	    if (e.when >= when) {
		break
	    }
	    lastEvalN = i
	}
	return(lastEvalN)
    }

    var getLastEval = function(when) {
	var n = getLastEvalN(when)
	if (n >= 0) {
	    return(evalList[n])
	}
	return(false)
    }

    var evalFromCursor = function(cm) {
        var selection;
	
        if (!connected) {
	    return(false);
        }
	
        if (cm.somethingSelected()) {
	    selection = cm.getSelection();
        }
        else {
	    var pos = cm.getCursor();
	    var nonempty = /\S/;
	    selection = cm.getLine(pos.line);
	    if (nonempty.test(selection)) {
		var above = pos.line - 1;
		// todo check for non-whitespace
		while (above >= 0 && nonempty.test(cm.getLine(above))) {
		    selection = cm.getLine(above) + "\n" + selection;
		    above--;
		}
		var below = pos.line + 1;
		while (below < cm.lineCount() && nonempty.test(cm.getLine(below))) {
		    selection = selection + "\n" + cm.getLine(below);
		    below++;
		}
		var c = cm.getCursor();
		
		// highlight evaluated area
		// make editor temporarily read only so that the
		// selection doesn't get overwritten by fast
		// typing..
		cm.setOption('readOnly', true);
		cm.setSelection({line: above+1, ch: 0}, {line: below, ch:0});
		setTimeout(function(){cm.setCursor(c);cm.setOption('readOnly', false);},100);
	    }
        }
	tidal(selection);
        // var spaces = Array(cm.getOption("indentUnit") + 1).join(" ");
        // cm.replaceSelection(spaces);
	return(selection)
    }
    
    var playbackEvent = function() {
	if (!playback) {
	    return(false)
	}
	var now = (new Date().getTime()) / 1000.0
	if (playback.n > changeList.length) {
	    playback = false
            $("#codediv").show();
            //$("#dummycodediv").hide();
	}
	else {
	    var nextChange = changeList[playback.n]
	    var lastEvalN = getLastEvalN(nextChange.when)
	    var nextTime = nextChange.when + playback.offset
	    var codepos
	    var code
	    delay = nextTime - now

	    if (lastEvalN > playback.lastEvalN) {
		e = evalList[lastEvalN]
		playback.lastEvalN = lastEvalN
		codepos = e.pos
		code = e.code
		console.log("got codepos " + codepos.line + "x" + codepos.ch)
	    }
	    
	    /*
	    console.log("> change: " + nextChange)
	    console.log("> nextTime: " + nextTime)
	    console.log("> now " + now)
	    console.log("> when: " + nextChange.when)
	    console.log("> delay " + delay)
	    */
	    console.log("delay: " + delay)
	    if (code) {
		//editor.setCursor(codepos)
		tidal(code)
		//evalFromCursor(editor)
	    }
	    setTimeout(
		function () {
		    history(playback.n);
		    playbackEvent()
		}, delay*1000.0
	    )
	}
	playback.n++
    }

    pulseClick = function() {
        if (lastTick > 0) {
            diff = ((new Date().getTime()) / 1000.0) - lastTick
            msg = "/pulse " + diff
            console.log(msg)
            ws.send(msg)
        }
    }

    pulseShift = function(secs) {
        console.log("shift " + secs)
        msg = "/pulse " + secs
        ws.send(msg)
    }

    faster = function() {
        console.log("faster")
        msg = "/faster"
        ws.send(msg)
    }

    slower = function() {
        console.log("slower")
        msg = "/slower"
        ws.send(msg)
    }

    var ws = new WebSocket('ws://localhost:9162', []);
    ws.onmessage = function (e) {
        var re = /\/(\w+) ((.|[\r\n])*)/
	var m = e.data.match(re);
	var ok = false;
	if (m) {
	    var status = m[1];
	    var data = m[2]
	    var code = m[3];
            if (status == 'eval') {
                var re2 = /([\d\.]+) ((.|\n)*)/
                var m2 = data.match(re2)
                var when = parseFloat(m2[1])
                var code = m2[2]
                console.log("Evaluated " + eval.code)
		$('div#errorbox').text("")
                ok = true
            }
            else if (status == 'bang') {
                var tick = parseInt(data)
                if (tick % 4 == 0) {
                    lastTick = (new Date().getTime()) / 1000.0
		    if (firstTick == null) {
			firstTick = lastTick
		    }
                    //console.log("bang " + tick)
                    $("#pulse").css("background", "#fff")
                }
                else if (tick % 4 == 1) {
                    $("#pulse").css("background", "#ccc")
                }
                else if (tick % 4 == 2) {
                    $("#pulse").css("background", "#bbb")
                }
                else {
                    $("#pulse").css("background", "#aaa")
                }
                ok = true
            }
            else if (status == 'error') {
		$('div#errorbox').text(data)
                ok = true
            }
	}
	if (!ok) {
	    console.log("msg: " + e.data)
	}
    };
    ws.onerror = function (error) { console.log("ws error" + error) };
    
    CodeMirror.commands.save = function() {
        var elt = editor.getWrapperElement();
        elt.style.background = "#def";
        setTimeout(function() { elt.style.background = ""; }, 300);
    };
    var editorOptions = {
        lineNumbers: true,
        mode: "text/x-haskell",
	saveFunction: saveState, // not working..
    };
    var ta = document.getElementById("codem")
    window.alert(ta)
    editor = CodeMirror.fromTextArea(ta, editorOptions);
    editor.setOption('readOnly', true);
    editor.setOption('theme', "solarized dark");
    
    ws.onopen = function () {
        // ws.send('/join alex'); // Send the message 'Ping' to the server
        connected = true;
        editor.setOption('readOnly', false);
    };
    
    editor.on('change',
              function (editor, change) {
		  var secs = (new Date().getTime()) / 1000.0;
		  // console.log("change: " + secs + " obj: " + change + " of " + change.from.line);
		  change.when = secs;
		  var changeCount = changeList.length;
		  if (changeCount % snapshotInterval == 0) {
		      change.snapshot = editor.getValue();
		      //console.log("snap");
		  }
		  
		  changeList.push(change);
		  console.log("changes: " + changeList.length);
		  if ((!sliding) && inHistory > -1) {
		      inHistory = -1;
		      $("#slider").slider("value", 100);
		  }
		  ws.send("/change " + JSON.stringify(change));

		  // return false so codemirror handles the event
		  return(false);
              }
	     );
    editor.setOption("extraKeys", {
        "Ctrl-.": function(cm) {
	    tidal("silence")
        },
        "Ctrl-0": function(cm) {
            ws.send("/panic")
        },
        "Ctrl-s": function(cm) {
            saveState()
        },
        "Ctrl-Enter": function(cm) {
	    var secs = (new Date().getTime()) / 1000.0;
	    var eval = {}
	    eval.when = secs
	    eval.pos = cm.getCursor()
	    eval.code = evalFromCursor(cm)
	    evalList.push(eval)
	    console.log("pushed " + eval)
	}
    });

    $("#restorefile").change(function() {
	var f = this.files[0];

	var reader = new FileReader();

	reader.onload = (function(theFile) {
            return function(e) {
		var data = JSON.parse(reader.result);
		evalList = data[0];
		changeList = data[1];

		// Force an update to the end of history
		historyPercent(1);
		editor.setValue(dummyEditor.getValue());
            };
	})(f);

	// Read in the image file as a data URL.
	reader.readAsText(f);
	$('#restorefile').hide();$('#loadbtn').show(); return(false)		  

    });

};
