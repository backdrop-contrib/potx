ABOUT
================================================================================

The goal of the Translation Template Extractor project is to provide 
command line and web based Gettext translation template extractor 
functionality for Drupal. These translation templates are used by 
teams to translate Drupal to their language of choice. There are 
basically two ways to use the contents of this project:

 * Copy potx.inc and potx-cli.php to the directory you would like to 
   generate translation templates for and run php potx-cli.php. 
   The translation templates will get generated as separate files in 
   the current directory.

 * Install the module on a Drupal site as you would with any other 
   module. Once potx module is turned on, you can go to the 
   "Extract strings" tab on the Locale administration interface, select 
   the module or modules you want to have a translation template for, 
   and submit the form. You will get one single template file generated.

The command line functionality is quite mature now, because it was 
basically carried over and refactored from extractor.php, previously 
hosted as part of the translation templates themselfs. The web based 
functionality is still in its early stages.
	
Note: If you only get a white browser screen as response to the 
extraction request, the memory limit for PHP on the server is probably 
too low, try to set that higher.

CREDITS
================================================================================

Command line extractor functionality orignally by 
Jacobo Tarrio <jtarrio [at] alfa21.com> (2003, 2004 Alfa21 Outsourcing)
Currently maintained by Gabor Hojtsy <gabor [at] hojtsy.hu>
