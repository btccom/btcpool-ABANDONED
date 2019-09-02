#!/usr/bin/env php
<?php
require_once __DIR__.'/../lib/init.php';
// PHP syntax for templates
// https://www.php.net/manual/control-structures.alternative-syntax.php
// https://www.php.net/manual/language.basic-syntax.phpmode.php

#
# kafka repeater cfg
#
# @since 2019-07
# @copyright btc.com
#
?>
kafka = {
    in_brokers = "<?=notNullTrim('kafka_in_brokers')?>";
    in_topic = "<?=notNullTrim('kafka_in_topic')?>";
    # Used to record progress / offsets.
    # Change it to reset the progress (will forward from the beginning).
    # The two repeater cannot have the same group id, otherwise the result is undefined.
    in_group_id = "<?=notNullTrim('kafka_in_group_id')?>";

    out_brokers = "<?=notNullTrim('kafka_out_brokers')?>";
    out_topic = "<?=notNullTrim('kafka_out_topic')?>";

<?php
$protocol = 'plaintext';

if (isTrue('kafka_out_use_ssl')):
    $protocol = 'ssl';

    $ca = notNullTrim('kafka_ssl_ca_content');
    $certificate = notNullTrim('kafka_ssl_certificate_content');
    $key = notNullTrim('kafka_ssl_key_content');

    if (notEmpty($ca)) file_put_contents('/tmp/ca.crt', $ca);
    if (notEmpty($certificate)) file_put_contents('/tmp/client.crt', $certificate);
    if (notEmpty($key)) file_put_contents('/tmp/client.key', $key);
endif;

$usr = optionalTrim('kafka_sasl_username');
$pwd = optionalTrim('kafka_sasl_password');

if (notEmpty($usr)):
    $protocol = 'sasl_'.$protocol;
endif;
?>
    # authorize settings (only for out_brokers)
    #
    # To generate a key and a self-signed certificate, run:
    # openssl genrsa -out client.key 2048
    # openssl req -new -key client.key -out client.crt.req
    # openssl x509 -req -days 365 -in client.crt.req -signkey client.key -out client.crt
    # cp client.crt ca.crt
    #
    security = {
        protocol = "<?=$protocol?>";
    };
<?php if (isTrue('kafka_out_use_ssl')): ?>
    ssl = {
    <?php if (notEmpty($ca)): ?>
        ca = {
            location = "/tmp/ca.crt";
        };
    <?php endif; ?>
    <?php if (notEmpty($certificate)): ?>
        certificate = {
            location = "/tmp/client.crt";
        };
    <?php endif; ?>
    <?php if (notEmpty($key)): ?>
        key = {
            location = "/tmp/client.key";
            password = "<?=optionalTrim('kafka_ssl_key_password')?>";
        };
    <?php endif; ?>
    };
<?php endif; // kafka_out_use_ssl ?>
<?php if (notEmpty($usr)): ?>
    sasl = {
        username = "<?=$usr?>";
        password = "<?=$pwd?>";
    };
<?php endif; // usr pwd ?>
};

###
# Choose a message converter you want to use. Set it to true to enable it.
# If no converter is enabled, kafka messages will be forwarded without any conversion.
# Currently, only one converter can be enabled at a time.
# TODO: Support for multiple converters (converter chain).
###

<?php
$convertor = strtolower(optionalTrim('message_convertor'));
?>

# Convert the Share version
# TODO: add bitcoin_v1_to_v2
share_convertor = {
    bitcoin_v2_to_v1 = <?=toString($convertor == 'share_convertor_bitcoin_v2_to_v1')?>;
};

# Change the difficulty of shares according to stratum jobs from a topic.
# TODO: add bitcoin_v2
<?php
if (in_array($convertor, [
    'share_diff_changer_bitcoin_v1',
    'share_diff_changer_bitcoin_v2_to_v1'
])):
?>
share_diff_changer = {
    job_brokers = "<?=notNullTrim('share_diff_changer_job_brokers')?>";
    job_topic = "<?=notNullTrim('share_diff_changer_job_topic')?>";
    job_group_id = "<?=notNullTrim('share_diff_changer_job_group_id')?>";

    bitcoin_v1 = <?=toString($convertor == 'share_diff_changer_bitcoin_v1')?>;
    bitcoin_v2_to_v1 = <?=toString($convertor == 'share_diff_changer_bitcoin_v2_to_v1')?>;

    # The program uses a deterministic difficulty copy logic.
    # This means that no matter when you run the program, the result is the same.
    # 
    # However, if you use the program to copy instant-generated shares, you will meet a problem:
    # Shares in a time period will be modified and forwarded only after got the stratum job within this period of time,
    # so current shares will always be stacked until a new job got later.
    # 
    # Setting a fixed time offset will make the current job available for the current and subsequent shares,
    # then shares will no longer be stacked.
    # When the time reaches the end of the offset, usually a new stratum job is ready.
    # So the copying process will become continuous rather than "pulsed".
    #
    # Since the difficulty changing of bitcoin (including BCH) is not very fast,
    # setting a time offset within 1 minute will not have a significant effect on users' earning.
    job_time_offset = <?=optionalTrim('share_diff_changer_job_time_offset', '30')?>;
};
<?php
endif; // share_diff_changer
?>

# Print shares only, no forwarding
share_printer = {
    bitcoin_v1 = <?=toString($convertor == 'share_printer_bitcoin_v1')?>;
};

# Print kafka messages, no forwarding
message_printer = {
    print_hex = <?=toString($convertor == 'message_printer_print_hex')?>;
};

# Logging options
log = {
    repeated_number_display_interval = <?=optionalTrim('log_repeated_number_display_interval', 10)?>; # seconds
};
