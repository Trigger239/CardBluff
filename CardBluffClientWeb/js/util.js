function startsWith(str, searchStr, position){
	if(str.startsWith)
		return str.startsWith(searchStr, position);
	if(position === undefined)
		position = 0;
	return str.slice(position, position + searchStr.length) === searchStr;
}