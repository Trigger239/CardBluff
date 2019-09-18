function startsWith(str, searchStr, position){
	if(str.startsWith)
		return str.startsWith(searchStr, position);
	return str.slice(position, position + searchStr.length) === searchStr;
}