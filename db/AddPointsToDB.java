import java.io.BufferedReader;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileReader;
import java.io.IOException;



public class AddPointsToDB {
	private static String erf;
	private static DBConnect dbc;
	private static final String DB_SERVER = "intensity.usc.edu";
	private static final String DB = "CyberShake";
	
	public static void main(String[] args) {
		if (args.length<2) {
			System.out.println("Usage: AddPointsToDB <ERF_ID> <directory> [server]");
			System.out.println("       AddPointsToDB <ERF_ID> <Source_ID> <Rupture_ID> <filename> [server]");
			System.out.println("  server defaults to " + DB_SERVER + " (MySQL); pass sqlite:<path> for SQLite.");
			System.exit(0);
		}

		erf = args[0];
		//AR: (2026-07-03) Optional last server argument, for SQLite. The original code only ever
		//AR: talked to the MySQL production DB_SERVER written in the code. Kept that as the default so
		//AR: existing SCEC invocations (no server arg) are unaffected.
		boolean isSQLite = false;
		String server = DB_SERVER;
		String lastArg = args[args.length-1];
		if (lastArg.indexOf("sqlite:") == 0) {
			server = lastArg;
			isSQLite = true;
		}
		//AR: dbc has to be built before either populate() runs. The original code left
		//AR: it null in the 2-arg (directory) mode, which threw a NullPointerException as soon as
		//AR: it tried to insert a point. Building it once here, up front, fixes both modes.
		dbc = new DBConnect(server, DB, isSQLite);

		int numArgs = isSQLite ? args.length-1 : args.length;
		if (numArgs==2) {
			populate(args[1]);
		} else if (numArgs<4) {
			System.out.println("Usage: AddPointsToDB <ERF_ID> <Source_ID> <Rupture_ID> <filename> [server]");
			System.exit(0);
		} else {
			populate(args[1], args[2], args[3]);
		}
		dbc.closeConnection();
	}

	private static void populate(String directory) {
		File dir = new File(directory);
		if (!dir.isDirectory()) {
			System.err.println(directory + " is not a directory.");
			System.exit(1);
		}

		File[] files = dir.listFiles();
		for (int i=0; i<files.length; i++) {
			String name = files[i].getName();
			String[] pieces = name.split("\\.");
			String prefix = pieces[0];
			pieces = prefix.split("_");
			System.out.println("Inserting source " + pieces[0] + ", rupture " + pieces[1] + " filename " + name);
			populate(pieces[0], pieces[1], files[i].getAbsolutePath());
		}
	}
	
	private static void populate(String source, String rupture, String filename) {
		BufferedReader br;
		String insertString = "insert into Points (ERF_ID, Source_ID, Rupture_ID, Lat, Lon, Depth, Rake, Dip, Strike) ";
		String valueString;
		try {
			br = new BufferedReader(new FileReader(filename));
			br.readLine(); //prob
			br.readLine(); //mag
			br.readLine(); //spacing
			br.readLine(); //rows
			br.readLine(); //cols
			br.readLine(); //comment line
			String line = br.readLine();
			int i=1;
			while (line!=null) {
				String[] pieces = line.split("\\s+");
				//Point_ID, ERF_ID, Source_ID, Rupture_ID, Lat, Lon, Depth, Rake, Dip, Strike
				valueString = "values (" + erf + ", " + source + ", " + rupture + ", " +
				pieces[0] + ", " + pieces[1] + ", " + pieces[2] + ", " + pieces[3] + ", " + pieces[4] + ", " + pieces[5] + ")";
				if (!dbc.insertData(insertString + valueString)) {
					System.err.println("Error inserting data with query " + insertString + valueString);
				}
//				System.out.println("inserting point " + i);
				line = br.readLine();
				i++;
			}
			br.close();
		} catch (FileNotFoundException e) {
			e.printStackTrace();
		} catch (IOException e) {
			e.printStackTrace();
		}

	}
}
