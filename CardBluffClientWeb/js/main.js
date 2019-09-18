const NICKNAME_SIZE_MIN = 3;
const NICKNAME_CHARS_NOT_ALLOWED = ":%\\";
const PASSWORD_SIZE_MIN = 4;
const FRAME_SIZE = 256;
const SOCKET_CONNECTION_TIMEOUT = 2000;

var ws;
var serverAddress = "localhost";
var serverPort = 2391;
var consoleOutput;
var textIO;

function StateBase(){};

StateBase.NOT_CONNECTED = 0;
StateBase.CONNECTION_LOST = 1;
StateBase.CONNECTING = 2;

StateBase.WAIT_RECONNECT = 3;

StateBase.NICKNAME_ENTER = 4;
StateBase.NICKNAME_SENT = 5;

StateBase.PASSWORD_REGISTER_FIRST_ENTER = 6;
StateBase.PASSWORD_REGISTER_FIRST_SENT = 7;

StateBase.PASSWORD_REGISTER_SECOND_ENTER = 8;
StateBase.PASSWORD_REGISTER_SECOND_SENT = 9;

StateBase.PASSWORD_ENTER = 10;
StateBase.PASSWORD_SENT = 11;

StateBase.AUTHORIZED = 12;
StateBase.AUTHORIZATION_FAILED = 13;


function State(){
	this.NOT_CONNECTED();
	//this.state = StateBase.NOT_CONNECTED;
}
	
State.prototype.NOT_CONNECTED = function(){
	this.state = StateBase.NOT_CONNECTED;
	textIO.inputModeNormal();
	textIO.inputDisable();
}

State.prototype.CONNECTING = function(){
	this.state = StateBase.CONNECTING;
	serverDataHide();
	serverAddress = document.getElementById("server_address").value;
	serverPort = document.getElementById("server_port").value;
	textIO.addLine("Connecting to " + serverAddress + ":" + serverPort + "...");
}

State.prototype.CONNECTION_LOST = function(){
	this.state = StateBase.CONNECTION_LOST;
	textIO.inputModeNormal();
	textIO.inputDisable();
}

State.prototype.WAIT_RECONNECT = function(){
	this.state = StateBase.WAIT_RECONNECT;
	textIO.addLine("Press Enter to try to connect again.");
	textIO.inputModeNormal();
	textIO.inputDisable();
	serverDataShow();
}

State.prototype.NICKNAME_ENTER = function(){
	this.state = StateBase.NICKNAME_ENTER;
	textIO.addLine("Please enter nickname.");
	textIO.inputModeNormal();
	textIO.inputEnable();
	textIO.inputFocus();
}

State.prototype.NICKNAME_SENT = function(){
	this.state = StateBase.NICKNAME_SENT;
	textIO.inputDisable();
}

State.prototype.PASSWORD_REGISTER_FIRST_ENTER = function(){
	this.state = StateBase.PASSWORD_REGISTER_FIRST_ENTER;
	textIO.inputModePassword();
	textIO.inputEnable();
	textIO.inputFocus();
}

State.prototype.PASSWORD_REGISTER_FIRST_SENT = function(){
	this.state = StateBase.PASSWORD_REGISTER_FIRST_SENT;
	textIO.inputDisable();
}

State.prototype.PASSWORD_REGISTER_SECOND_ENTER = function(salt){
	this.state = StateBase.PASSWORD_REGISTER_SECOND_ENTER;
	this.salt = salt;
	textIO.inputModePassword();
	textIO.inputEnable();
	textIO.inputFocus();
}

State.prototype.PASSWORD_REGISTER_SECOND_SENT = function(){
	this.state = StateBase.PASSWORD_REGISTER_SECOND_SENT;
	textIO.inputDisable();
}

State.prototype.PASSWORD_ENTER = function(salt){
	this.state = StateBase.PASSWORD_ENTER;
	this.salt = salt;
	textIO.inputModePassword();
	textIO.inputEnable();
	textIO.inputFocus();
}

State.prototype.PASSWORD_SENT = function(){
	this.state = StateBase.PASSWORD_SENT;
	textIO.inputDisable();
}

State.prototype.AUTHORIZED = function(){
	this.state = StateBase.AUTHORIZED;
	textIO.inputModeNormal();
	textIO.inputEnable();
	textIO.inputFocus();
}

State.prototype.AUTHORIZATION_FAILED = function(){
	this.state = StateBase.AUTHORIZATION_FAILED;
	textIO.inputDisable();
	textIO.inputModeNormal();
}

State.prototype.get = function(){
	return state;
}

var state;

//from https://stackoverflow.com/questions/17191945/conversion-between-utf-8-arraybuffer-and-string
//(slightly modified)
function utf8ArrayToString(array) {
	var out, i, len, c;
	var char2, char3;

	out = "";
	len = array.length;
	i = 0;
	while (i < len) {
		c = array[i++];
		if(c == 0)
			return out;
		switch (c >> 4){ 
		case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
			// 0xxxxxxx
			out += String.fromCharCode(c);
			break;
		case 12: case 13:
			// 110x xxxx   10xx xxxx
			char2 = array[i++];
			if(char2 == 0)
				return out;
			out += String.fromCharCode(((c & 0x1F) << 6) | (char2 & 0x3F));
			break;
		case 14:
			// 1110 xxxx  10xx xxxx  10xx xxxx
			char2 = array[i++];
			if(char2 == 0)
				return out;
			char3 = array[i++];
			if(char3 == 0)
				return out;
			out += String.fromCharCode(((c & 0x0F) << 12) |
									   ((char2 & 0x3F) << 6) |
									   ((char3 & 0x3F) << 0));
			break;
		}
	}    
	return out;
}

function stringToUtf8Array(str, length) {
	var len = str.length;
	var i = 0;
	var array = [];
	
	while (i < len) {
		var c = str.charCodeAt(i++);
		
		if(c <= 0x7F){ // 0xxxxxxx
			array.push(c);
		}
		else if(c <= 0x7FF){ // 110x xxxx   10xx xxxx
			array.push(0xC0 | ((c >> 6) & 0x1F));
			array.push(0x80 | (c & 0x3F));
		}
		else{ // 1110 xxxx  10xx xxxx  10xx xxxx
			array.push(0xE0 | ((c >> 12) & 0x0F));
			array.push(0x80 | ((c >> 6) & 0x3F));
			array.push(0x80 | (c & 0x3F));
		}
	}    
	
	if(length && array.length < length){
		var len = array.length;
		array.length = length;
		array.fill(0, len, length);
	}
	
	return new Uint8Array(array);
}

function serverMessageHandler(event){
	var data = event.data; //data should be ArrayBuffer
	var i = 0;
	
	data = new Uint8Array(data);
	
	while(data.length > 0){	
		var str = utf8ArrayToString(data);
		var st = state.state;
		
		if(startsWith(str, "password?"))
			state.PASSWORD_ENTER(str.substr("password?".length));
		else if(startsWith(str, "password_first?"))
			state.PASSWORD_REGISTER_FIRST_ENTER();
		else if(startsWith(str, "password_second?"))
			state.PASSWORD_REGISTER_SECOND_ENTER(str.substr("password_second?".length));
		else if(str == "auth_ok!")
			state.AUTHORIZED();
		else if(str == "auth_fail!")
			state.AUTHORIZATION_FAILED();
		else
			textIO.addLineColored(str);
			
		data = data.slice(FRAME_SIZE);
	}
}

function validate_nickname(str){
	if(str.length < NICKNAME_SIZE_MIN){
		textIO.addLineColored(ERROR_PREFIX + " Nickname should contain at least " + NICKNAME_SIZE_MIN + " characters!");
		textIO.addLine("Please try again.");
		return false;		
	}
	
	for(var i = 0; i < NICKNAME_CHARS_NOT_ALLOWED.length; i++){
		if(str.indexOf(NICKNAME_CHARS_NOT_ALLOWED[i]) != -1){
			textIO.addLineColored(ERROR_PREFIX + " Nickname shouldn't contain these characters: " + NICKNAME_CHARS_NOT_ALLOWED);
			textIO.addLine("Please try again.");
			return false;
		}
	}
	
	return true;
}

function validate_password(str){
	if(str.length < PASSWORD_SIZE_MIN){
		textIO.addLineColored(ERROR_PREFIX + " Password should contain at least " + PASWORD_SIZE_MIN + " characters!");
		textIO.addLine("Please try again.");
		return false;		
	}
	
	return true;
}

function sendStringToServer(str){
	ws.send(stringToUtf8Array(str, FRAME_SIZE));
}

function inputHandler(str){
	var st = state.state;
	if(st == StateBase.NICKNAME_ENTER){
		if(!validate_nickname(str))
			return;
		
		sendStringToServer("nickname:" + str);
		state.NICKNAME_SENT();
	}
	else if(st == StateBase.PASSWORD_REGISTER_FIRST_ENTER){
		if(!validate_password(str))
			return;
				
		sendStringToServer("password_first:" + sha256(str));
		state.PASSWORD_REGISTER_FIRST_SENT();
	}
	else if(st == StateBase.PASSWORD_REGISTER_SECOND_ENTER){
		if(!validate_password(str))
			return;
		
		sendStringToServer("password_second:" + sha256(sha256(str) + state.salt));
		state.PASSWORD_REGISTER_SECOND_SENT();
	}
	else if(st == StateBase.PASSWORD_ENTER){
		if(!validate_password(str))
			return;				
		
		sendStringToServer("password:" + sha256(sha256(str) + state.salt));
		state.PASSWORD_SENT();
	}
	else if(st == StateBase.AUTHORIZED){
		sendStringToServer(str);
	}
}

var connectionCheckTimeout;

function socketOpenHandler(){
	connectionCheckTimeout = setTimeout(socketCheckConnectionHandler, SOCKET_CONNECTION_TIMEOUT);
}

function socketCheckConnectionHandler(){
	if(ws.readyState == WebSocket.OPEN){ //the connection is still open
		textIO.addLine("Connection established!");
		state.NICKNAME_ENTER();
	}
}

function socketErrorHandler(event){
	ws.close();
}

function socketCloseHandler(event){
	clearTimeout(connectionCheckTimeout);
	if(state.state == StateBase.CONNECTING){
		textIO.addLineColored(ERROR_PREFIX + " Server unavailable!");
	}
	else{
		textIO.addLineColored(ERROR_PREFIX + " Connection is lost!");
	}
	state.WAIT_RECONNECT();
}

function keyboardHandler(event){
	if(state.state == StateBase.WAIT_RECONNECT){
		if(event.which == 13 || event.keyCode == 13 || event.key == "Enter"){
			connect();
		}
	}
}

function connect(){
	state.CONNECTING();
	ws = new WebSocket("ws://" + serverAddress + ":" + serverPort);
	
	ws.binaryType = "arraybuffer";
	ws.onopen = socketOpenHandler;
	ws.onmessage = serverMessageHandler;
	ws.onerror = socketErrorHandler;
	ws.onclose = socketCloseHandler;
}

function serverDataShow(){
	document.getElementById("server_data").classList.add("shown");
}

function serverDataHide(){
	document.getElementById("server_data").classList.remove("shown");
}

window.onload = function(){
	textIO = new TextIO(document.getElementById("console_input"), 
						document.getElementById("console_output"), 
						inputHandler);
	state = new State;

	serverDataShow();
	document.getElementById("connect_button").onclick = connect;

	window.onkeypress = keyboardHandler;
}