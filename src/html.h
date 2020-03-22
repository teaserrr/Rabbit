#ifndef html_h
#define html_h

const char RABBIT_HTTP_HEADER[] = 
"<!DOCTYPE html>" \
"<html lang=\"en\">" \
"  <head>" \
"    <meta charset=\"UTF-8\">" \
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>" \
"    <title>{device}</title>";
const char RABBIT_HTTP_STYLE[] = 
"   <style>" \
"   body { font-family: \"Helvetica Neue\", Helvetica, Arial; font-size: 14px; line-height: 20px; font-weight: 400; color: #f6f6f6; background: #2b2b2b; }" \
"   .wrapper { margin: auto; padding: 40px; }" \
"   .table { margin: 0 0 20px 0; color: #3b3b3b; width: 50%; box-shadow: 0 1px 3px rgba(0, 0, 0, 0.2); display: table; }" \
"   @media screen and (max-width: 550px) { .table {	width: 100%; } }" \
"   .row { display: table-row; background: #f6f6f6; }" \
"   .row:nth-of-type(odd) { background: #e9e9e9; }" \
"   .row.header { font-weight: 900; color: #ffffff; background: #ea6153; }" \
"   .row.green { background: #27ae60; }" \
"   .row.blue { background: #2980b9; }" \
"   .cell { padding: 6px 12px; display: table-cell; }" \
"   .button { width: 50%; box-shadow: 0 1px 3px rgba(0, 0, 0, 0.2); display: table; margin: 0 0 10px 0; font-weight: 900; color: #ffffff; background: #ea6153; padding: 6px 0px; text-decoration: none; text-align: center; }" \
"   .button.green { background: #27ae60; }" \
"   .button.blue { background: #2980b9; }" \
"   @media screen and (max-width: 550px) { .button {width: 100%; } }" \
"   </style>";
const char RABBIT_HTTP_BODY[] = 
"  </head>" \
"  <body>" \
"    <h1>{device}</h1>" \
"    <div class=\"wrapper\">";
const char RABBIT_HTTP_TABLE[] =
"      <div class=\"table\">" \
"        <div class=\"row header\">" \
"          <div class=\"cell\">Sensor data</div>" \
"          <div class=\"cell\"></div>" \
"        </div>";
const char RABBIT_HTTP_ITEM[] = 
"        <div class=\"row\">" \
"          <div class=\"cell\">{key}:</div>" \
"          <div class=\"cell\">{value}{unit}</div>" \
"        </div>";
const char RABBIT_HTTP_TABLE_END[] =
"      </div>";
const char RABBIT_HTTP_LINK_BUTTON[] =
"      <a class=\"button\" href=\"configuration\">Configuration</a>";
const char RABBIT_HTTP_END[] = 
"    </div>" \
"  </body>" \
"</html>";

#endif