var sleepInterval = 50;
var garbageCollectionAfterLoop = true;

function test()
{
	for (var index = 0; index < arguments.length; ++index) {
		print(typeof arguments[index])
	}	
	return;
}

function main()
{
	print("Initializing programm ...")
}

function loop()
{
	return 1;
}