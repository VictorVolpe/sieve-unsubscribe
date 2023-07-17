require ["variables", "envelope", "subaddress", "vnd.dovecot.execute"];

if envelope :matches :detail "To" "*" {
  set :lower "list" "${1}";
  if envelope :matches "From" "*" { set :lower "from" "${1}"; }
  if envelope :matches :domain "To" "*" { set :lower "domain" "${1}"; }
  
  if not execute "unsubscribe" ["${from}", "${domain}", "${list}"] {
    stop;
  }
}

discard;