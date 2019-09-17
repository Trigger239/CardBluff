const NICKNAME_SIZE_MIN = 3;
const NICKNAME_CHARS_NOT_ALLOWED = ":%\\";
const PASSWORD_SIZE_MIN = 4;
const FRAME_SIZE = 256;
const SOCKET_CONNECTION_TIMEOUT = 2000;

let ws;
let serverAddress = "localhost";
let serverPort = 2391;
let consoleOutput;
let textIO;

class StateBase{
	static get NOT_CONNECTED(){return 0;}
	static get CONNECTION_LOST(){return 1;}
	static get CONNECTING(){return 2;}

	static get WAIT_RECONNECT(){return 3;}

	static get NICKNAME_ENTER(){return 4;}
	static get NICKNAME_SENT(){return 5;}

	static get PASSWORD_REGISTER_FIRST_ENTER(){return 6;}
	static get PASSWORD_REGISTER_FIRST_SENT(){return 7;}

	static get PASSWORD_REGISTER_SECOND_ENTER(){return 8;}
	static get PASSWORD_REGISTER_SECOND_SENT(){return 9;}

	static get PASSWORD_ENTER(){return 10;}
	static get PASSWORD_SENT(){return 11;}

	static get AUTHORIZED(){return 13;}
	static get AUTHORIZATION_FAILED(){return 14;}
}

class State{
	constructor(){
		this.NOT_CONNECTED();
		//this.state = StateBase.NOT_CONNECTED;
	}
	
	NOT_CONNECTED(){
		this.state = StateBase.NOT_CONNECTED;
		textIO.inputModeNormal();
		textIO.inputDisable();
	}
	
	CONNECTING(){
		this.state = StateBase.CONNECTING;
		serverDataHide();
		serverAddress = document.getElementById("server_address").value;
		serverPort = document.getElementById("server_port").value;
		textIO.addLine("Connecting to " + serverAddress + ":" + serverPort + "...");
	}
	
	CONNECTION_LOST(){
		this.state = StateBase.CONNECTION_LOST;
		textIO.inputModeNormal();
		textIO.inputDisable();
	}
	
	WAIT_RECONNECT(){
		this.state = StateBase.WAIT_RECONNECT;
		textIO.addLine("Press Enter to try to connect again.");
		textIO.inputModeNormal();
		textIO.inputDisable();
		serverDataShow();
	}
	
	NICKNAME_ENTER(){
		this.state = StateBase.NICKNAME_ENTER;
		textIO.addLine("Please enter nickname.");
		textIO.inputModeNormal();
		textIO.inputEnable();
		textIO.inputFocus();
	}
	
	NICKNAME_SENT(){
		this.state = StateBase.NICKNAME_SENT;
		textIO.inputDisable();
	}
	
	PASSWORD_REGISTER_FIRST_ENTER(){
		this.state = StateBase.PASSWORD_REGISTER_FIRST_ENTER;
		textIO.inputModePassword();
		textIO.inputEnable();
		textIO.inputFocus();
	}
	
	PASSWORD_REGISTER_FIRST_SENT(){
		this.state = StateBase.PASSWORD_REGISTER_FIRST_SENT;
		textIO.inputDisable();
	}
	
	PASSWORD_REGISTER_SECOND_ENTER(salt){
		this.state = StateBase.PASSWORD_REGISTER_SECOND_ENTER;
		this.salt = salt;
		textIO.inputModePassword();
		textIO.inputEnable();
		textIO.inputFocus();
	}
	
	PASSWORD_REGISTER_SECOND_SENT(){
		this.state = StateBase.PASSWORD_REGISTER_SECOND_SENT;
		textIO.inputDisable();
	}
	
	PASSWORD_ENTER(salt){
		this.state = StateBase.PASSWORD_ENTER;
		this.salt = salt;
		textIO.inputModePassword();
		textIO.inputEnable();
		textIO.inputFocus();
	}
	
	PASSWORD_SENT(){
		this.state = StateBase.PASSWORD_SENT;
		textIO.inputDisable();
	}
	
	AUTHORIZED(){
		this.state = StateBase.AUTHORIZED;
		textIO.inputModeNormal();
		textIO.inputEnable();
		textIO.inputFocus();
	}
	
	AUTHORIZATION_FAILED(){
		this.state = StateBase.AUTHORIZATION_FAILED;
		textIO.inputDisable();
		textIO.inputModeNormal();
	}
	
	get(){
		return state;
	}
};

let state;

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
	let len = str.length;
	let i = 0;
	let array = [];
	
	while (i < len) {
		let c = str.charCodeAt(i++);
		
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
		let len = array.length;
		array.length = length;
		array.fill(0, len, length);
	}
	
	return new Uint8Array(array);
}

function serverMessageHandler(event){
	let data = event.data; //data should be ArrayBuffer
	let i = 0;
	
	data = new Uint8Array(data);
	
	while(data.length > 0){	
		let str = utf8ArrayToString(data);
		let st = state.state;
		
		if(str.startsWith("password?"))
			state.PASSWORD_ENTER(str.substr("password?".length));
		else if(str.startsWith("password_first?"))
			state.PASSWORD_REGISTER_FIRST_ENTER();
		else if(str.startsWith("password_second?"))
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
	
	for(let i = 0; i < NICKNAME_CHARS_NOT_ALLOWED.length; i++){
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
	let st = state.state;
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

let connectionCheckTimeout;

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