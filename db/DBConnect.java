
import java.sql.*;
import java.io.*;
import java.util.*;

/**
 *
 * <p>Title:DBConnect </p>
 * <p>Description: Interface to the CME database</p>
 * @author: Nitin Gupta & Vipin Gupta & Phil Maechling
 * @version 1.0
 */
public class DBConnect
{

  private Connection conn = null;
  private String hostName;
  private String dbName;
  private boolean isSQLite = false;
  private int port = 3306;

  /**
   *class constructor
   */
  public DBConnect(String hostname, String dbName)
  {
    this.hostName = hostname;
    this.dbName = dbName;
    try {
      getConnection();
    }
    catch (Exception ex) {
      ex.printStackTrace();
    }
  }
  
  public DBConnect(String hostname, String dbName, boolean isSQLite)
  {
    this.hostName = hostname;
    this.dbName = dbName;
    this.isSQLite = isSQLite;
    try {
      getConnection();
    }
    catch (Exception ex) {
      ex.printStackTrace();
    }
  }
  

  /**
   * Runs the select query on the database
   * @param query
   * @return
   */
  public ResultSet selectData(String query)
  {
    ResultSet result =null;
    Statement stat= null;
    try
    {
      getConnection();
      stat = conn.createStatement();
      //System.out.println(query);
      //gets the resultSet after running the query
      result = stat.executeQuery(query+";");
    }
    catch (SQLException ex)
    {
      ex.printStackTrace();
      while (ex != null)
      {
        ex.printStackTrace();
        //ex = ex.getNextException();
      }
    }
    catch (Exception ex)
    {
        ex.printStackTrace();
    }
    finally
    {
      try
      {
        //stat.close();
        //conn.close();
      } catch (Exception e)
      {
        e.printStackTrace();
      }
    }
    return result;
  }


  /**
   * Runs the inserts query on the database
   * @param query
   */
  public boolean insertData(String query)
  {

    Statement stat = null;

    try
    {
       getConnection();
      stat = conn.createStatement();
      //System.out.println(query);
      //executes the query
      stat.executeUpdate(query+";");
      return true;
    }
    catch (SQLException ex)
    {
      while (ex != null)
      {
        ex.printStackTrace();
        ex = ex.getNextException();
      }
    }
    catch (Exception ex)
    {
      ex.printStackTrace();
    }
    finally
    {
      try
      {
        //stat.close();
        //AR: closing after every single insertData() call is fine for MySQL (cheap reconnect to a
        //AR: live server) but is very slow for SQLite, since each open and close re-reads
        //AR: the whole file and re-does journal setup on GPFS. AddPointsToDB.java calls this once
        //AR: per point (tens of thousands of times for a single rupture set), which made a bulk
        //AR: load effectively unbounded in time. Left open for SQLite; original MySQL behavior
        //AR: unchanged.
        if (!isSQLite) conn.close();
      } catch (Exception e)
      {
        e.printStackTrace();
      }
    }
    return false;
  }


  /**
   * Runs the Delete and update operation on the tables in the database
   * @param query
   */
  public boolean deleteOrUpdateData(String query)
  {
    Statement stat = null;
    try
    {
      getConnection();
      stat = conn.createStatement();
      //executes the delete or update query
      stat.execute(query+";");
      //commits the result of the query to the database
      stat.execute("commit;");
      stat.close();
      return true;
    }
    catch (SQLException ex)
    {
      while (ex != null)
      {
        ex.printStackTrace();
        ex = ex.getNextException();
      }
    }
    catch (Exception ex)
    {
      ex.printStackTrace();
    }
    finally
    {
      try
      {
        stat.close();
        conn.close();
      } catch (Exception e)
      {
        e.printStackTrace();
      }
    }
    return false;
  }

  /**
   * Establishes the connection with the database using the mysql driver
   *
   * @return the database connection
   * @return
   * @throws SQLException
   * @throws IOException
   * @throws Exception
   */


  private void getConnection() throws SQLException, IOException,Exception {

    if(conn!=null && !conn.isClosed()) return ;
//    String drivers = "org.gjt.mm.mysql.Driver";
    //AR: this was fixed to the MySQL driver even in SQLite mode. It is the same cause I already fixed
    //AR: in CyberCommands/data/RunID.java. Without this, isSQLite connections fail outright unless
    //AR: a MySQL driver jar happens to also be on the classpath (harmless but misleading).
    String drivers = isSQLite ? "org.sqlite.JDBC" : "com.mysql.jdbc.Driver";
    //System.out.println("dbname is:"+dbName);
    String url = "jdbc:mysql://"+hostName+":"+port+"/"+dbName+"?serverTimezone=America/Los_Angeles";
    String username = "scottcal";
    String password = "CyberShake2007";
    if (isSQLite) {
    	url = "jdbc:" + hostName;
    }
//    String password = "";

    //Try to load the driver, if this fails then print an error
    //and the contents of the stack
    try
    {
      Class.forName(drivers).newInstance();
    }
    catch (Exception E)
    {
      E.printStackTrace();
      throw new Exception("*** Unable to load database driver ***\n");
    }

     try
     {
    	 if (isSQLite) {
    	     	conn = DriverManager.getConnection(url);    		 
    	 } else {
    		 conn = DriverManager.getConnection(url,username,password);
    	 }
     }
     //Catch the exception, throw a new DBException
     catch (SQLException E)
     {
       throw new Exception("*** Unable to connect to the database ***"+
                           "\nSQL Message: "+E.getMessage()+
                           "\nSQL ErrorCode: "+E.getErrorCode()+
                           "\nSQL State: "+E.getSQLState()+"\n");
     }
     return;
  }
  
  public void closeConnection() {
  	try {
		conn.close();
		conn = null;
	} catch (SQLException e) {
		e.printStackTrace();
	}
  }
}