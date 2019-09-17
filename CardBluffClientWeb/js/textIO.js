'use strict'

const MAX_OUTPUT_ROWS = 10000;

const SERVER_PREFIX	= "SERVER:";
const CARDS_PREFIX 	= "CARDS:";
const USER_PREFIX 	= "USER ";
const ERROR_PREFIX	= "ERROR:";

const COLOR_ESCAPE = "%";

const HEARTS	= 0;
const DIAMONDS	= 1;
const SPADES	= 2;
const CLUBS		= 3;

/*
const HEARTS_CHAR 	= "&hearts;";
const DIAMONDS_CHAR	= "&diams;";
const SPADES_CHAR 	= "&spades;";
const CLUBS_CHAR	= "&clubs;";
*/
const HEARTS_CHAR 	= "&hearts;";
const DIAMONDS_CHAR	= "&diams;";
const SPADES_CHAR 	= "&#x2664;";
const CLUBS_CHAR	= "&#x2667;";


class TextIO{

	constructor(input, output, inputHandler){
		this.input = input;
		this.output = output;
		this.inputHandler = inputHandler;
		this.input.textIO = this;
		this.input.onkeypress = this.inputProcess;
	};
	
	static htmlEscape(html){
		return html.replace(/&/g, "&amp;")
			.replace(/</g, "&lt;")
			.replace(/>/g, "&gt;")
			.replace(/"/g, "&quot;")
			.replace(/'/g, "&#39;")
			.replace(/\//g, "&#x2F;")
			.replace(/`/g, "&#x60;")
			.replace(/=/g, "&#x3D;");
	}
	
	static getColoredText(str, _class){
		let span = document.createElement("span");
		span.className = _class;
		span.innerHTML = TextIO.htmlEscape(str);
		return span;
	}
	
	static getColoredString(str, _class){
		return '<span class="' + _class + '">' + TextIO.htmlEscape(str) + '</span>';
	}
	
	static highlightString(str){
		let highlightOn = false;
		let backslashFound = false;
		
		let res = "";
		let highlightBuffer = "";
		
		for(let i = 0; i < str.length; i++){
			if(backslashFound){
				if(highlightOn){
					if(str[i] != "\\" && str[i] != COLOR_ESCAPE){
						highlightBuffer += "\\";
					}
					highlightBuffer += str[i];
				}
				else{
					if(str[i] != "\\" && str[i] != COLOR_ESCAPE){
						res += TextIO.htmlEscape("\\");
					}
					res += TextIO.htmlEscape(str[i]);
				}
				backslashFound = false;
			}
			else if(str[i] == "\\"){
				backslashFound = true;
			}
			else if(str[i] == COLOR_ESCAPE){
				highlightOn = !highlightOn;
				if(highlightOn){
					highlightBuffer = "";
				}
				else{
					res += TextIO.getColoredString(highlightBuffer, "msg_highlighted");
				}
			}
			else{
				if(highlightOn){
					highlightBuffer += str[i];
				}
				else{
					res += TextIO.htmlEscape(str[i]);
				}
			}
		}
		if(highlightOn){
			res += TextIO.getColoredString(highlightBuffer, "msg_highlighted");	
		}
		if(backslashFound){
			if(highlightOn)
				res += TextIO.getColoredString("\\", "msg_highlighted");
			else
				res += TextIO.htmlEscape("\\");
		}
		
		return res;
	}
	
	scrollOutputToBottom(){
		this.output.scrollBy(0, 100000000); //This is a big number
	}
	
	outputAppend(element){
		this.output.appendChild(element);
		if(this.output.children.length > MAX_OUTPUT_ROWS)
			this.output.firstChild.remove();
		this.scrollOutputToBottom();
	}

	addLine(str, _class){
		let p = document.createElement("p");
		if(_class != undefined)
			p.className = _class;
		p.innerHTML = TextIO.htmlEscape(str);
		this.outputAppend(p);
	}
	
	addLineColored(str){
		let p = document.createElement("p");
		
		if(str.startsWith(SERVER_PREFIX)){
			p.appendChild(TextIO.getColoredText(SERVER_PREFIX, "msg_server_prefix"));
			p.innerHTML += TextIO.highlightString(str.substr(SERVER_PREFIX.length));
		}
		else if(str.startsWith(CARDS_PREFIX)){
			let cards = [];
			
			let i = CARDS_PREFIX.length; //prefix
			let j = str.indexOf(":", i);
			if(j == -1)
				return false;
			let card_number = parseInt(str.substr(i, j));
			if(isNaN(card_number))
				return false;
			i = j + 1;
			
			for(let k = 0; k < card_number; k++){
				j = str.indexOf(":", i);
				
				if(j == -1){
					if(k != card_number - 1)
						return false;
					else
						j = str.length;
				}
				
				let comma = str.indexOf(",", i);
				if(comma == -1 || comma >= j)
					return false;
				let suit = parseInt(str.substr(i, comma));
				if(isNaN(suit))
					return false;
				let value = str[comma + 1];
				cards.push({suit: suit, value: value});
				
				i = j + 1;
			}

			str = str.substr(i);
			
			p.appendChild(TextIO.getColoredText(SERVER_PREFIX, "msg_server_prefix"));
			p.innerHTML += TextIO.highlightString(" " + str);
			
			let first = true;
			for(let card in cards){
				p.innerHTML += TextIO.htmlEscape(cards[card].value);
				
				switch(cards[card].suit){
				case HEARTS:
					p.innerHTML += '<span class="suit_hearts">' + HEARTS_CHAR + '</span>';
					break;
				case DIAMONDS:
					p.innerHTML += '<span class="suit_diamonds">' + DIAMONDS_CHAR + '</span>';
					break;
				case SPADES:
					p.innerHTML += '<span class="suit_spades">' + SPADES_CHAR + '</span>';
					break;
				case CLUBS:
					p.innerHTML += '<span class="suit_clubs">' + CLUBS_CHAR + '</span>';
					break;
				default:
					p.innerHTML += TextIO.htmlEscape("?");
				}
				
				if(card != cards.length - 1){
					p.innerHTML += TextIO.htmlEscape(", ");
				}
			}
		}
		else if(str.startsWith(ERROR_PREFIX)){
			p.appendChild(TextIO.getColoredText(str, "msg_error"));
		}
		else if(str.startsWith(USER_PREFIX)){
			p.innerHTML += TextIO.highlightString(str.substr(USER_PREFIX.length));
		}
		else{ //Unknown message type
			p.innerHTML += TextIO.htmlEscape("> " + str);
		}
		
		this.outputAppend(p);
	}
	
	inputModeNormal(){
		this.input.type = "text";
	}
	
	inputModePassword(){
		this.input.type = "password";
	}
	
	inputEnable(){
		this.input.disabled = false;
	}
	
	inputDisable(){
		this.input.disabled = true;
	}
	
	inputClear(){
		this.input.value = "";
	}
	
	inputFocus(){
		this.input.focus();
	}
	
	inputProcess(event){
		if(event.which == 13 || event.keyCode == 13 || event.key == "Enter"){
			let str = this.value;
			
			//skip empty input
			if(str.length == 0)
				return false;
				
			if(this.type == "password"){
				this.textIO.addLine("*".repeat(str.length), "msg_user_input");
			}
			else{
				let p = document.createElement("p");
				p.className = "msg_user_input";
				p.innerHTML = TextIO.highlightString(str);
				this.textIO.outputAppend(p);
			}
				
			if(this.textIO.inputHandler)
				this.textIO.inputHandler(this.textIO.input.value);
				
			this.textIO.input.value = "";
			
			return false;
		}
		return true;
	}

};
