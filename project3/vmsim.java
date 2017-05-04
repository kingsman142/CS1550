public class vmsim{
	public static void main(String[] args){
		int numFrames = -1;
		int refresh = -1;
		String algorithm = "";
		String traceFile = "";

		if(args.length == 5){ //Arguments without the optional refresh argument
			if(!args[0].equals("-n")) return;
			numFrames = Integer.parseInt(args[1]);
			if(!args[2].equals("-a")) return;
			algorithm = args[3];
			traceFile = args[4];
		} else if(args.length == 7){ //Arguments with the optional refresh argument
			if(!args[0].equals("-n")) return;
			numFrames = Integer.parseInt(args[1]);
			if(!args[2].equals("-a")) return;
			algorithm = args[3];
			if(!args[4].equals("-r")) return;
			refresh = Integer.parseInt(args[5]);
			traceFile = args[6];
		} else{ //Program requires either 5 or 7 arguments; reject otherwise
			System.out.println("Invalid number of command-line arguments!");
			return;
		}

		VMSimAlgorithms simulation = new VMSimAlgorithms(numFrames, traceFile);
		if(algorithm.equals("rand")) simulation.random();
		else if(algorithm.equals("opt")) simulation.optimal();
		else if(algorithm.equals("clock")) simulation.clock();
		else if(algorithm.equals("nru")) simulation.nru(refresh);
		else System.out.println("That algorithm doesn't exist!");
	}
}
