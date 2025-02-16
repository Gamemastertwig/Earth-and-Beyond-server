using System;
using System.Data;
using System.Windows.Forms;
using System.Xml;


namespace Station_Tools
{
    static class Program
    {
        /// <summary>
        /// The main entry point for the application.
        /// </summary>
        [STAThread]
        static void Main()
        {
            //Attempt to load in the login information from the config file
            try
            {
                DataSet m_Data = new DataSet();
                DataTable m_Table;
                DataRow m_Row;

                m_Data.ReadXml(Application.StartupPath + "\\Config.xml");
                m_Table = m_Data.Tables[0];
                m_Row = m_Table.Rows[0];
                SQLData.Host = m_Row.ItemArray[0].ToString();
                SQLData.Port = Int32.Parse(m_Row.ItemArray[1].ToString());
                SQLData.User = m_Row.ItemArray[2].ToString();
                SQLData.Pass = m_Row.ItemArray[3].ToString();
            }
            catch
            {
                SQLData.Host = "";
                SQLData.Port = 0;
                SQLData.User = "";
                SQLData.Pass = "";
            }

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            Login LoginBox = new Login();

            // Add in the params
            LoginBox.LoginUsername.Text = SQLData.User;
            LoginBox.LoginPassword.Text = SQLData.Pass;
            LoginBox.SQLServer.Text = SQLData.Host;
            LoginBox.SQLPort.Text = SQLData.Port.ToString();

            // Show the box
            LoginBox.ShowDialog();

            // Read the values back
            if (!LoginBox.m_Cancel && LoginBox.m_HasChanged)
            {
                SQLData.User = LoginBox.LoginUsername.Text;
                SQLData.Pass = LoginBox.LoginPassword.Text;
                SQLData.Host = LoginBox.SQLServer.Text;
                SQLData.Port = System.Convert.ToInt32(LoginBox.SQLPort.Text, 10);

                // Save in config
                XmlTextWriter textWriter = new XmlTextWriter(Application.StartupPath + "\\Config.xml", null);

                textWriter.Formatting = Formatting.Indented;
                // Start writing
                textWriter.WriteStartDocument();
                // Start main element
                textWriter.WriteStartElement("Confg");
                // Write Host
                textWriter.WriteStartElement("Host");
                textWriter.WriteString(SQLData.Host.ToString());
                textWriter.WriteEndElement();
                // Write Port
                textWriter.WriteStartElement("Port");
                textWriter.WriteString(SQLData.Port.ToString());
                textWriter.WriteEndElement();
                // Write User
                textWriter.WriteStartElement("User");
                textWriter.WriteString(SQLData.User.ToString());
                textWriter.WriteEndElement();
                // Write Password
                textWriter.WriteStartElement("Pass");
                textWriter.WriteString(SQLData.Pass.ToString());
                textWriter.WriteEndElement();
                // Write it all to the file
                textWriter.WriteEndElement();
                textWriter.WriteEndDocument();
                // Close it
                textWriter.Close();
            }            
            LoginBox.Close();

            // Load if not cancled
            if (!LoginBox.m_Cancel)
                Application.Run(new Main());
        }
    }
}